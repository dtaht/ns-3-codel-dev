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
  NS_LOG_FUNCTION_NOARGS ();
  INIT_LIST_HEAD(&flowchain);
  this->q = CreateObject<CoDelQueue> ();
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
                   UintegerValue (4507),
                   MakeUintegerAccessor (&Fq_CoDelQueue::m_quantum),
                   MakeUintegerChecker<uint32_t> ())
    ;
  return tid;
}

Fq_CoDelQueue::Fq_CoDelQueue () :
  m_ht (),
  psource (),
  peturbation (psource.GetInteger(0,std::numeric_limits<std::size_t>::max()))
{
  NS_LOG_FUNCTION_NOARGS ();
  INIT_LIST_HEAD(&m_new_flows);
  INIT_LIST_HEAD(&m_old_flows);
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
      std::size_t h = (string_hash((format("%x%x%x%x")
                                    % (ip_hd.GetDestination().Get())
                                    % (ip_hd.GetSource().Get())
                                    % (ip_hd.GetProtocol())
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

  Fq_CoDelSlot *slot;

  std::size_t h = Fq_CoDelQueue::hash(p);
  NS_LOG_DEBUG ("fq_codel enqueue use queue "<<h);
  if (m_ht[h] == NULL)
    {
      NS_LOG_DEBUG ("fq_codel enqueue Create queue " << h);
      m_ht[h] = new Fq_CoDelSlot ();
      slot = m_ht[h];
      slot->q->backlog = &backlog;
      slot->h = h;
    } 
  else 
    {
      slot = m_ht[h];
    }

  queued = slot->q->Enqueue(p);

  if (queued)
    {
      slot->backlog += p->GetSize();
      backlog += p->GetSize();

      if (list_empty(&slot->flowchain)) {
        NS_LOG_DEBUG ("fq_codel enqueue inactive queue "<<h);
        list_add_tail(&slot->flowchain, &m_new_flows);
        slot->deficit = m_quantum;
      }
    }
  else
    {
      Drop (p);
    }
  NS_LOG_DEBUG ("fq_codel enqueue "<<slot->h<<" "<<queued);
  return queued;
}

Ptr<Packet>
Fq_CoDelQueue::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);
  Fq_CoDelSlot *flow;
  struct list_head *head;

begin:
  head = &m_new_flows;
  if (list_empty(head)) {
    head = &m_old_flows;
    if (list_empty(head))
      return NULL;
  }
  flow = list_first_entry(head, Fq_CoDelSlot, flowchain);

  NS_LOG_DEBUG ("fq_codel scan "<<flow->h);

  if (flow->deficit <= 0) 
    {
      flow->deficit += m_quantum;
      NS_LOG_DEBUG ("fq_codel deficit now "<<flow->deficit<<" "<<flow->h);
      list_move_tail(&flow->flowchain, &m_old_flows);
      goto begin;
    }

  Ptr<Packet> p = flow->q->Dequeue();
  if (p == NULL)
    {
      /* force a pass through old_flows to prevent starvation */
      if ((head == &m_new_flows) && !list_empty(&m_old_flows))
        list_move_tail(&flow->flowchain, &m_old_flows);
      else
        list_del_init(&flow->flowchain);
      goto begin;

    }
  NS_LOG_DEBUG ("fq_codel found a packet "<<flow->h);
      
  flow->deficit -= p->GetSize();
  flow->backlog -= p->GetSize();
  backlog -= p->GetSize();

  return p; 
}

Ptr<const Packet>
Fq_CoDelQueue::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);

  struct list_head *head;

  head = &m_new_flows;
  if (list_empty(head)) {
    head = &m_old_flows;
    if (list_empty(head))
      return 0;
  }
  return list_first_entry(head, Fq_CoDelSlot, flowchain)->q->Peek();
}

} // namespace ns3

