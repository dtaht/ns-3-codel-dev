/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 University of Washington
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <limits>
#include "ns3/log.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "fq_codel-queue.h"
#include "ns3/red-queue.h"
#include "ns3/ipv4-header.h"
#include "ns3/ppp-header.h"
#include <boost/functional/hash.hpp>
#include <boost/format.hpp>

/*
 * SFQ as implemented by Linux, not the classical version.
 */

NS_LOG_COMPONENT_DEFINE ("Fq_CoDelQueue");

using namespace boost;

namespace ns3 {

Fq_CoDelSlot::Fq_CoDelSlot () :
  h(0)
{
  this->q = CreateObject<CoDelQueue> ();
  NS_LOG_FUNCTION_NOARGS ();
}

Fq_CoDelSlot::~Fq_CoDelSlot ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

NS_OBJECT_ENSURE_REGISTERED (Fq_CoDelQueue);

TypeId Fq_CoDelQueue::GetTypeId (void) 
{
  static TypeId tid = TypeId ("ns3::Fq_CoDelQueue")
    .SetParent<Queue> ()
    .AddConstructor<Fq_CoDelQueue> ()
    .AddAttribute ("headMode",
                   "Add new flows in the head position",
                   BooleanValue (false),
                   MakeBooleanAccessor (&Fq_CoDelQueue::m_headmode),
                   MakeBooleanChecker ())
    .AddAttribute ("peturbInterval",
                   "Peterbation interval in packets",
                   UintegerValue (500000),
                   MakeUintegerAccessor (&Fq_CoDelQueue::m_peturbInterval),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Quantum",
                   "Quantum in bytes",
                   UintegerValue (9000),
                   MakeUintegerAccessor (&Fq_CoDelQueue::m_quantum),
                   MakeUintegerChecker<uint32_t> ())
    ;
  return tid;
}

Fq_CoDelQueue::Fq_CoDelQueue () :
  m_ht (),
  m_new_flows(),
  m_old_flows(),
  psource (),
  peturbation (psource.GetInteger(0,std::numeric_limits<std::size_t>::max()))
{
  NS_LOG_FUNCTION_NOARGS ();
}

Fq_CoDelQueue::~Fq_CoDelQueue ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

std::size_t
Fq_CoDelQueue::hash(Ptr<Packet> p)
{
  boost::hash<std::string> string_hash;

  Ptr<Packet> q = p->Copy();

  class PppHeader ppp_hd;

  q->RemoveHeader(ppp_hd);

  class Ipv4Header ip_hd;
  if (q->PeekHeader (ip_hd))
    {
      if (pcounter > m_peturbInterval)
        peturbation = psource.GetInteger(0,std::numeric_limits<std::size_t>::max());
      std::size_t h = (string_hash((format("%x%x%d")
                                    % (ip_hd.GetDestination().Get())
                                    % (ip_hd.GetSource().Get())
                                    % (peturbation)).str())
                       & 0x2ff);
      return h;
    }
  else
    {
      return 0;
    }
}

bool 
Fq_CoDelQueue::DoEnqueue (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);
  bool queued;

  Ptr<Fq_CoDelSlot> slot;

  std::size_t h = Fq_CoDelQueue::hash(p);
  NS_LOG_DEBUG ("fq_codel enqueue use queue "<<h);
  if (m_ht[h] == NULL)
    {
      NS_LOG_DEBUG ("fq_codel enqueue Create queue " << h);
      m_ht[h] = slot = Create<Fq_CoDelSlot> ();
      slot->q->backlog = &backlog;
      slot->h = h;
    } 
  else 
    {
      slot = m_ht[h];
    }

  if (!slot->active) 
    {
      NS_LOG_DEBUG ("fq_codel enqueue inactive queue "<<h);

      slot->deficit = m_quantum;
    }

  queued = slot->q->Enqueue(p);

  if (queued)
    {
      slot->backlog += p->GetSize();
      backlog += p->GetSize();

      if (m_headmode && !slot->active) {
        m_new_flows.push_front(slot);
      } else {
        m_new_flows.push_back(slot);
      }
      slot->active = true;
    }
  else
    {
      Drop (p);
    }
  NS_LOG_DEBUG ("fq_codel enqueue "<<slot->h<<" "<<m_new_flows.size()<<" "<<m_old_flows.size()<<" "<<queued);
  return queued;
}

Ptr<Packet>
Fq_CoDelQueue::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<Fq_CoDelSlot> slot;

 next_slot:
  if (m_new_flows.empty()) 
    {
      if (m_old_flows.empty()) 
        {
          NS_LOG_DEBUG ("fq_codel dequeue found no flows");
          return 0;
        }
      else
        {
          NS_LOG_DEBUG ("fq_codel run old flows");
          m_new_flows.splice(m_new_flows.end(), m_old_flows);
          goto next_slot;
        }
    }
  slot = m_new_flows.front();
  NS_LOG_DEBUG ("fq_codel scan "<<slot->h<<" "<<m_new_flows.size()<<" "<<m_old_flows.size());
  m_new_flows.pop_front();
  slot->active = false;

  if (slot->deficit <= 0) 
    {
      slot->deficit += m_quantum;
      NS_LOG_DEBUG ("fq_codel deficit now "<<slot->deficit<<" "<<slot->h);
      m_old_flows.push_back(slot);
      slot->active = true;
      goto next_slot;
    }

  if (slot->q->GetQueueSize() == 0)
    {
      NS_LOG_DEBUG ("fq_codel slot empty "<<slot->h);
      goto next_slot;
    }

  Ptr<Packet> p = slot->q->Dequeue();
  if (p != NULL)
    {
      NS_LOG_DEBUG ("fq_codel found a packet "<<slot->h);
      
      slot->deficit -= p->GetSize();
      slot->backlog -= p->GetSize();
      backlog -= p->GetSize();

      if (slot->q->GetQueueSize() != 0)
        {
          NS_LOG_DEBUG ("fq_codel more packets in flow "<<slot->h);
          slot->active = true;
        }
      return p;
    } 
  else 
    {
      goto next_slot;
    }
}

Ptr<const Packet>
Fq_CoDelQueue::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);

  if (!m_new_flows.empty())
    {
      return m_new_flows.front()->q->Peek();
    }
  else
    {
      return 0;
    }
}

} // namespace ns3

