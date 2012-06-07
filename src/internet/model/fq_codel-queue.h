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

#ifndef FQ_CODEL_H
#define FQ_CODEL_H

#include <queue>
#include "ns3/random-variable.h"
#include "ns3/boolean.h"
#include "ns3/packet.h"
#include "ns3/queue.h"
#include <map>
#include "ns3/codel-queue.h"

namespace ns3 {

class TraceContainer;

class Fq_CoDelSlot : public SimpleRefCount<Fq_CoDelSlot> {
public:
  friend class CoDelQueue;

  // static TypeId GetTypeId (void);
  Fq_CoDelSlot ();

  virtual ~Fq_CoDelSlot();

  Ptr<CoDelQueue> q;
  int deficit;
  uint32_t backlog;
  int h;
  bool active;
};

/**
 * \ingroup queue
 */
class Fq_CoDelQueue : public Queue {
public:
  static TypeId GetTypeId (void);
  /**
   * \brief Fq_CoDelQueue Constructor
   */
  Fq_CoDelQueue ();

  virtual ~Fq_CoDelQueue();

private:
  virtual bool DoEnqueue (Ptr<Packet> p);
  virtual Ptr<Packet> DoDequeue (void);
  virtual Ptr<const Packet> DoPeek (void) const;

  std::size_t hash(Ptr<Packet> p);
  // only mutable so we can get a reference out of here in Peek()
  mutable std::map<int, Ptr<Fq_CoDelSlot> > m_ht;
  mutable std::list<Ptr<Fq_CoDelSlot> > m_new_flows;
  mutable std::list<Ptr<Fq_CoDelSlot> > m_old_flows;
  uint32_t m_divisor;
  uint32_t m_buckets;
  uint32_t m_peturbInterval;
  bool m_headmode;
  mutable size_t pcounter;
  UniformVariable psource;
  mutable uint32_t peturbation;
  uint32_t m_quantum;
  uint32_t backlog;
};

} // namespace ns3

#endif /* FQ_CODEL_H */
