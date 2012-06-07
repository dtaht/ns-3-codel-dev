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
#include "sfq-queue.h"
#include "ns3/red-queue.h"
#include "ns3/ipv4-header.h"
#include "ns3/ppp-header.h"
#include <boost/functional/hash.hpp>
#include <boost/format.hpp>

/*
 * SFQ as implemented by Linux, not the classical version.
 */

NS_LOG_COMPONENT_DEFINE ("SfqQueue");

using namespace boost;

namespace ns3 {

SfqSlot::SfqSlot () :
  allot(0),
  backlog(0),
  h(0),
  active(false)
{
  this->q = CreateObject<RedQueue> ();
  NS_LOG_FUNCTION_NOARGS ();
}

SfqSlot::~SfqSlot ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

NS_OBJECT_ENSURE_REGISTERED (SfqQueue);

TypeId SfqQueue::GetTypeId (void) 
{
  static TypeId tid = TypeId ("ns3::SfqQueue")
    .SetParent<Queue> ()
    .AddConstructor<SfqQueue> ()
    .AddAttribute ("headMode",
                   "Add new flows in the head position",
                   BooleanValue (false),
                   MakeBooleanAccessor (&SfqQueue::m_headmode),
                   MakeBooleanChecker ())
    .AddAttribute ("peturbInterval",
                   "Peterbation interval in packets",
                   UintegerValue (500),
                   MakeUintegerAccessor (&SfqQueue::m_peturbInterval),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("Quantum",
                   "Quantum in bytes",
                   UintegerValue (4500),
                   MakeUintegerAccessor (&SfqQueue::m_quantum),
                   MakeUintegerChecker<uint32_t> ())
    ;
  return tid;
}

SfqQueue::SfqQueue () :
  m_ht (),
  m_flows(),
  psource (),
  peturbation (psource.GetInteger(0,std::numeric_limits<std::size_t>::max()))
{
  NS_LOG_FUNCTION_NOARGS ();
}

SfqQueue::~SfqQueue ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

std::size_t
SfqQueue::hash(Ptr<Packet> p)
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
SfqQueue::DoEnqueue (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);
  bool queued;

  Ptr<SfqSlot> slot;

  std::size_t h = SfqQueue::hash(p);
  if (m_ht[h] == NULL)
    {
      NS_LOG_DEBUG ("SFQ enqueue Create queue " << h);
      m_ht[h] = slot = Create<SfqSlot> ();
      slot->h = h;
      slot->backlog = 0;
      slot->allot = m_quantum;
    } 
  else 
    {
      NS_LOG_DEBUG ("SFQ enqueue use queue "<<h);
      slot = m_ht[h];
    }

  if (!slot->active) 
    {
      NS_LOG_DEBUG ("SFQ enqueue inactive queue "<<h);
      if (m_headmode) {
        m_flows.push_front(slot);
      } else {
        m_flows.push_back(slot);
      }
    }
  slot->active = true;

  uint32_t sz = p->GetSize();

  if ((queued = slot->q->Enqueue(p))) {
    slot->backlog += sz;
  }

  return queued;
}

Ptr<Packet>
SfqQueue::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  if (m_flows.empty()) {
    return 0;
  }

  Ptr<SfqSlot> slot;

 next_slot:
  slot = m_flows.front();
  NS_LOG_DEBUG ("SFQ scan "<<slot->h);

  m_flows.pop_front();

  if (slot->allot <= 0) {
    slot->allot += m_quantum;
    m_flows.push_back(slot);
    goto next_slot;
  }

  if (slot->q->Peek() != 0)
    {
      NS_LOG_DEBUG ("SFQ found a packet "<<slot->h);
      Ptr<Packet> p = slot->q->Dequeue();
      
      slot->backlog -= p->GetSize();
      slot->allot -= p->GetSize();
      
      if (slot->q->Peek() != 0)
        {
          m_flows.push_back(slot);
        }
      else
        {
          slot->active = false;
        }
      return p;
    } 
  else 
    {
      NS_LOG_DEBUG ("SFQ found empty queue "<<slot->h);
      slot->active = false;
      return 0;
    }
}

Ptr<const Packet>
SfqQueue::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);

  if (!m_flows.empty())
    {
      return m_flows.front()->q->Peek();
    }
  else
    {
      return 0;
    }
}

} // namespace ns3

