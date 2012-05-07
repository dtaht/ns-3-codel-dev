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

NS_LOG_COMPONENT_DEFINE ("SfqQueue");

using namespace boost;

namespace ns3 {

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
    ;
  return tid;
}

SfqQueue::SfqQueue () :
  m_ht (),
  nextbucket (0),
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

Ptr<RedQueue>
SfqQueue::getQ(Ptr<Packet> p) 
{
  NS_LOG_FUNCTION (this << p);

  std::size_t h = SfqQueue::hash(p);
  if (m_ht[h] == NULL)
    {
      NS_LOG_DEBUG ("Create queue " << h);
      m_ht[h] = CreateObject<RedQueue> ();
      if (m_headmode)
        nextbucket=h;
    }
  return m_ht[h];
}

// return the next non-empty queue
Ptr<RedQueue>
SfqQueue::getQ() const
{
  NS_LOG_FUNCTION_NOARGS ();
  //  mutable std::map<int, Ptr<RedQueue> > m_ht;
  std::pair< std::map<int, Ptr<RedQueue> >::iterator, 
             std::map<int, Ptr<RedQueue> >::iterator 
             > ret;

  Ptr<RedQueue> q = NULL;
  std::map<int, Ptr<RedQueue> >::iterator m_ht_it;

  NS_LOG_DEBUG ("Bucket search " << nextbucket);

  ret = m_ht.equal_range(nextbucket);
  if (ret.first == m_ht.end())
    {
      if (m_ht.begin() != m_ht.end()) 
        {
          NS_LOG_DEBUG ("Bucket wrap");
          // nextbucket is greater than the last one in use
          // get the first one
          ret = m_ht.equal_range(0);
        }
      else
        {
          NS_LOG_DEBUG ("No buckets");
          return NULL;
        }
    }
  q = ret.first->second;
  // q now points to a queue, so...
  while ((q->GetQueueSize() == 0) && (ret.first != m_ht.end()))
    {
      nextbucket = ret.first->first;
      NS_LOG_DEBUG ("Bucket looking in " << nextbucket);
      q = ret.first->second;
      ++ret.first;
    }
  NS_LOG_DEBUG ("Bucket found in " << nextbucket);
  ++nextbucket;
  if (q->GetQueueSize() == 0)
    {
      m_ht.erase(ret.first->first);
      return NULL;
    }
  else
    return q;
}


bool 
SfqQueue::DoEnqueue (Ptr<Packet> p)
{
  NS_LOG_FUNCTION (this << p);

  Ptr<RedQueue> q = getQ(p);

  return q->Enqueue(p);
}

Ptr<Packet>
SfqQueue::DoDequeue (void)
{
  NS_LOG_FUNCTION (this);

  Ptr<RedQueue> q = getQ();

  if (q != NULL)
    {
      ++pcounter;
      Ptr<Packet> p = q->Dequeue();
      return p;
    }
  else
    {
      return NULL;
    }
}

Ptr<const Packet>
SfqQueue::DoPeek (void) const
{
  NS_LOG_FUNCTION (this);

  Ptr<RedQueue> q = getQ();

  if (q != NULL)
    {
      return q->Peek();
    }
  else
    {
      return NULL;
    }
}

} // namespace ns3

