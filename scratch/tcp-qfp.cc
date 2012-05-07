/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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
 *
 */

// Based on tcp-star-server.cc from the ns3 distribution, with copious cut and paste from elsewhere

// uses boost::format
// Thus the ns3 build step needs to be something like this:
// CXXFLAGS="-I/usr/include -O3" ./waf -d debug --enable-examples --enable-tests configure
// My ubuntu system puts the boost headers in /usr/include/boost
// I had to install the package "libboost-dev" to get them

// Default Network topology, dumbbell network with N nodes each side
// N (default 15) servers
// N-M (default 5) clients have R2 (default 40 Mbps) bandwidth
//    but also have between 100 and 102 ms delay
// M (default 10) clients have R1 (default 10 Mbps) bandwidth
//    but delays in the 1 to 2 ms range
// Bottleneck link has R (default 100 Mbps) bandwidht, 1 ms delay
// Server links are identical to the bottleneck
// Bottleneck link queue can be RED or DropTail, many parameters adjustable

// Usage examples for things you might want to tweak:
// List possible arguments:
//       ./waf --run="tcp-qfp --PrintHelp"
// Run with default arguments:
//       ./waf --run="tcp-qfp"
// Set lots of parameters:
//       ./waf --run="tcp-qfp --appDataRate=15Mbps --R1=10Mbps --R2=30Mbps --nNodes=15 --mNodes=10 --R=90Mbps --queueType=RED"

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>

#include <boost/format.hpp>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"

using namespace ns3;
using namespace boost;

NS_LOG_COMPONENT_DEFINE ("TcpServer");

int 
main (int argc, char *argv[])
{
  // Users may find it convenient to turn on explicit debugging
  // for selected modules; the below lines suggest how to do this

  LogComponentEnable ("TcpServer", LOG_LEVEL_INFO);
  LogComponentEnable ("PointToPointNetDevice", LOG_LEVEL_INFO);
  //LogComponentEnable ("TcpL4Protocol", LOG_LEVEL_ALL);
  //LogComponentEnable ("PacketSink", LOG_LEVEL_ALL);
  //LogComponentEnable ("SfqQueue", LOG_LEVEL_ALL);

  // turn on checksums
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  uint32_t N = 15; //number of nodes
  uint32_t M = 10; //number of low latency nodes
  std::string appDataRate = "10Mbps";
  std::string bottleneckRate = "100Mbps";
  std::string CoDelInterval = "100ms";
  std::string CoDelTarget = "5ms";
  std::string R1 = "10Mbps";
  std::string R2 = "40Mbps";
  std::string Q1queueType = "DropTail";
  std::string Q2queueType = "DropTail";
  double      Q1minTh = 50;
  double      Q1maxTh = 80;
  uint32_t    Q1maxPackets = 100;
  double      Q2minTh = 50;
  double      Q2maxTh = 80;
  uint32_t    Q2maxPackets = 100;
  double      minTh = 50;
  double      maxTh = 80;
  uint32_t    modeBytes  = 0;
  uint32_t    stack  = 0;
  uint32_t    modeGentle  = 0;
  uint32_t    maxPackets = 100;
  uint32_t    pktSize = 1400;
  uint32_t    sfqheadmode = 0;
  uint32_t maxBytes = 0;
  std::string queueType = "DropTail";

  double AppStartTime   = 0.1001;

  // cubic is the default congestion algorithm in Linux 2.6.26
  std::string tcpCong = "cubic";
  // the name of the NSC stack library that should be used
  std::string nscStack = "liblinux2.6.26.so";

  // Allow the user to override any of the defaults and the above
  // Config::SetDefault()s at run-time, via command-line arguments
  CommandLine cmd;
  // cmd.AddValue ("appDataRate", "Set OnOff App DataRate", appDataRate);
  cmd.AddValue ("maxBytes",
                "Total number of bytes for application to send", maxBytes);
  cmd.AddValue ("queueType", "Set Queue type to CoDel, DropTail, RED, or SFQ", queueType);
  cmd.AddValue ("modeBytes", "Set RED Queue mode to Packets <0> or bytes <1>", modeBytes);
  cmd.AddValue ("stack", "Set TCP stack to NSC <0> or linux-2.6.26 <1> (warning, linux stack is really slow in the sim)", stack);
  cmd.AddValue ("modeGentle", "Set RED Queue mode to standard <0> or gentle <1>", modeBytes);
  cmd.AddValue ("maxPackets","Max Packets allowed in the queue", maxPackets);
  cmd.AddValue ("nNodes", "Number of client nodes", N);
  cmd.AddValue ("mNodes", "Number of low latency client noodes", M);
  cmd.AddValue ("R", "Bottleneck rate", bottleneckRate);
  cmd.AddValue ("R1", "Low latency node edge link bottleneck rate", R1);
  cmd.AddValue ("Q1Type", "Set Queue type to DropTail or RED", Q1queueType);
  cmd.AddValue ("Q1redMinTh", "RED queue minimum threshold (packets)", Q1minTh);
  cmd.AddValue ("Q1redMaxTh", "RED queue maximum threshold (packets)", Q1maxTh);
  cmd.AddValue ("Q1maxPackets","Max Packets allowed in the queue", Q1maxPackets);
  cmd.AddValue ("R2", "High latency node edge link bottleneck rate", R2);
  cmd.AddValue ("Q2Type", "Set Queue type to DropTail or RED", Q2queueType);
  cmd.AddValue ("Q2redMinTh", "RED queue minimum threshold (packets)", Q2minTh);
  cmd.AddValue ("Q2redMaxTh", "RED queue maximum threshold (packets)", Q2maxTh);
  cmd.AddValue ("Q2maxPackets","Max Packets allowed in the queue", Q2maxPackets);
  cmd.AddValue ("redMinTh", "RED queue minimum threshold (packets)", minTh);
  cmd.AddValue ("redMaxTh", "RED queue maximum threshold (packets)", maxTh);
  cmd.AddValue ("SFQHeadMode", "New SFQ flows go to the head", sfqheadmode);
  cmd.AddValue ("Interval", "CoDel algorithm interval", CoDelInterval);
  cmd.AddValue ("Target", "CoDel algorithm target queue delay", CoDelTarget);
  cmd.Parse (argc, argv);

  if ((queueType != "RED") && (queueType != "DropTail") && (queueType != "SFQ") && (queueType != "CoDel"))
    {
      NS_ABORT_MSG ("Invalid queue type: Use --queueType=RED or --queueType=DropTail");
    }
  if ((Q1queueType != "RED") && (Q1queueType != "DropTail"))
    {
      NS_ABORT_MSG ("Invalid Q1 queue type: Use --queueType=RED or --queueType=DropTail");
    }
  if ((Q2queueType != "RED") && (Q2queueType != "DropTail"))
    {
      NS_ABORT_MSG ("Invalid Q2 queue type: Use --queueType=RED or --queueType=DropTail");
    }

  if (modeGentle)
    {
      Config::SetDefault ("ns3::RedQueue::Gentle", BooleanValue (true));
    }

  if (sfqheadmode)
    {
      Config::SetDefault ("ns3::SfqQueue::headMode", BooleanValue (true));
    }

  Config::SetDefault ("ns3::SfqQueue::peturbInterval", UintegerValue(131));

  Config::SetDefault ("ns3::RedQueue::LInterm", DoubleValue (50));
  if (!modeBytes)
    {
      Config::SetDefault ("ns3::DropTailQueue::Mode", StringValue ("Packets"));
      Config::SetDefault ("ns3::DropTailQueue::MaxPackets", UintegerValue (maxPackets));
      Config::SetDefault ("ns3::RedQueue::Mode", StringValue ("Packets"));
      Config::SetDefault ("ns3::RedQueue::QueueLimit", UintegerValue (maxPackets));
    }
  else 
    {
      Config::SetDefault ("ns3::DropTailQueue::Mode", StringValue ("Bytes"));
      Config::SetDefault ("ns3::DropTailQueue::MaxBytes", UintegerValue (maxPackets * pktSize));
      Config::SetDefault ("ns3::RedQueue::Mode", StringValue ("Bytes"));
      Config::SetDefault ("ns3::RedQueue::QueueLimit", UintegerValue (maxPackets * pktSize));
      Q1maxPackets *= pktSize;
      Q2maxPackets *= pktSize;
      minTh *= pktSize; 
      maxTh *= pktSize;
      Q1minTh *= pktSize; 
      Q1maxTh *= pktSize;
      Q2minTh *= pktSize; 
      Q2maxTh *= pktSize;
    }

  Config::SetDefault ("ns3::RedQueue::MinTh", DoubleValue (minTh));
  Config::SetDefault ("ns3::RedQueue::MaxTh", DoubleValue (maxTh));
  Config::SetDefault ("ns3::RedQueue::LinkBandwidth", StringValue (bottleneckRate));
  Config::SetDefault ("ns3::RedQueue::LinkDelay", StringValue ("1ms"));

  Config::SetDefault ("ns3::OnOffApplication::PacketSize", UintegerValue (pktSize));
  Config::SetDefault ("ns3::TcpSocket::SegmentSize", UintegerValue (pktSize));
  Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue (appDataRate));

  NS_LOG_INFO ("Create nodes.");
  NodeContainer serverNodes;
  NodeContainer leftNode;
  NodeContainer rightNode;
  NodeContainer clientNodes;
  serverNodes.Create (N);
  clientNodes.Create (N);
  leftNode.Create (1);
  rightNode.Create (1);
  NodeContainer allNodes = NodeContainer (serverNodes, leftNode, rightNode, clientNodes);

  for(uint32_t i=0; i<N; ++i)
    {
      Names::Add ((format("/Names/Servers/Server%d") % (i+1)).str(), serverNodes.Get (i));
    }
  for(uint32_t i=0; i<N; ++i)
    {
      Names::Add ((format("/Names/Clients/Client%d") % (i+1)).str(), clientNodes.Get (i));
    }
  Names::Add ("/Names/Left", leftNode.Get (0));
  Names::Add ("/Names/Right", rightNode.Get (0));


  // Install network stacks on the nodes
  InternetStackHelper internet;
  if (stack)
    {
      internet.SetTcp ("ns3::NscTcpL4Protocol","Library",StringValue (nscStack));
      // Could set something other than Cubic here, but setting Cubic explicitly exits the sim
      if (tcpCong != "cubic")
        Config::Set ("/NodeList/*/$ns3::Ns3NscStack<linux2.6.26>/net.ipv4.tcp_congestion_control", StringValue (tcpCong));
    }

  internet.Install (serverNodes);
  internet.Install (clientNodes);

  InternetStackHelper internet2;
  internet2.Install (leftNode);
  internet2.Install (rightNode);

  //Collect an adjacency list of nodes for the p2p topology
  std::vector<NodeContainer> nodeAdjacencyList (2*N+1);
  for(uint32_t i = N, j = 0; i<2*N; ++i, ++j)
    {
      nodeAdjacencyList[i] = NodeContainer (leftNode, clientNodes.Get (j));
    }
  for(uint32_t i=0; i<N; ++i)
    {
      nodeAdjacencyList[i] = NodeContainer (rightNode, serverNodes.Get (i));
    }
  nodeAdjacencyList[2*N] = NodeContainer (leftNode, rightNode);


  // We create the channels first without any IP addressing information
  NS_LOG_INFO ("Create channels.");
  PointToPointHelper p1channel;
  p1channel.SetDeviceAttribute ("DataRate", StringValue (R1));
  if (Q1queueType == "RED")
    {
      p1channel.SetQueue ("ns3::RedQueue",
                          "MinTh", DoubleValue (Q1minTh),
                          "MaxTh", DoubleValue (Q1maxTh),
                          "LinkBandwidth", StringValue (R1),
                          "QueueLimit",  UintegerValue (Q1maxPackets));
    }
  PointToPointHelper p2channel;
  p2channel.SetDeviceAttribute ("DataRate", StringValue (R2));
  if (Q2queueType == "RED")
    {
      p2channel.SetQueue ("ns3::RedQueue",
                          "MinTh", DoubleValue (Q2minTh),
                          "MaxTh", DoubleValue (Q2maxTh),
                          "LinkBandwidth", StringValue (R2),
                          "QueueLimit",  UintegerValue (Q2maxPackets));
    }  
  PointToPointHelper serverchannel;
  // server links are fast
  serverchannel.SetDeviceAttribute ("DataRate", StringValue (bottleneckRate));
  serverchannel.SetChannelAttribute ("Delay", StringValue ("1ms"));
  PointToPointHelper bottleneckchannel;
  // last one has different properties
  bottleneckchannel.SetDeviceAttribute ("DataRate", StringValue (bottleneckRate));
  bottleneckchannel.SetChannelAttribute ("Delay", StringValue ("1ms"));
  if (queueType == "RED")
    {
      bottleneckchannel.SetQueue ("ns3::RedQueue",
                                  "MinTh", DoubleValue (minTh),
                                  "MaxTh", DoubleValue (maxTh),
                                  "LinkBandwidth", StringValue (bottleneckRate),
                                  "LinkDelay", StringValue ("1ms"));
    } 
  else if (queueType == "SFQ")
    {
      bottleneckchannel.SetQueue ("ns3::SfqQueue");
    } 
  else if (queueType == "CoDel")
    {
      bottleneckchannel.SetQueue ("ns3::CoDelQueue",
                                  "Interval", StringValue(CoDelInterval),
                                  "Target", StringValue(CoDelTarget),
                                  "MinBytes", UintegerValue(1500)
                                  );
    }

  UniformVariable shortrtt (1,2);
  UniformVariable longrtt (160,166);

  // order devices are put in here ends up being numbering order
  std::vector<NetDeviceContainer> deviceAdjacencyList (2*N+1);
  for(uint32_t i=0; i<deviceAdjacencyList.size ()-1; ++i)
    {
      // N-M clients have more bandwith but long RTT
      if (i>(N+M-1))
        {
          double rn = longrtt.GetValue ();
          p2channel.SetChannelAttribute ("Delay", StringValue ((format("%gms") % rn).str()));
          deviceAdjacencyList[i] = p2channel.Install (nodeAdjacencyList[i]);
          NS_LOG_INFO ((format("device %d (%s-%s) at R2=%s, RTT %gms") % i 
                        % Names::FindName(nodeAdjacencyList[i].Get (0))
                        % Names::FindName(nodeAdjacencyList[i].Get (1))
                        % R2 % rn).str());
        }
      else if ((i>(N-1)) && (i<(N+M-1)))
        {
          double rn = shortrtt.GetValue ();
          p1channel.SetChannelAttribute ("Delay", StringValue ((format("%gms") % rn).str()));
          deviceAdjacencyList[i] = p1channel.Install (nodeAdjacencyList[i]);
          NS_LOG_INFO ((format("device %d (%s-%s) at R1=%s, RTT %gms") % i 
                        % Names::FindName(nodeAdjacencyList[i].Get (0))
                        % Names::FindName(nodeAdjacencyList[i].Get (1))
                        % R1 % rn).str());
        }
      else
        {
          deviceAdjacencyList[i] = serverchannel.Install (nodeAdjacencyList[i]);
        }
    }

  deviceAdjacencyList[2*N] = bottleneckchannel.Install (nodeAdjacencyList[2*N]);

  // Later, we add IP addresses.
  NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  std::vector<Ipv4InterfaceContainer> interfaceAdjacencyList (2*N+1);
  for(uint32_t i=0; i<interfaceAdjacencyList.size (); ++i)
    {
      std::ostringstream subnet;
      subnet<<"10.1."<<i+1<<".0";
      NS_LOG_INFO (subnet.str ().c_str ());
      ipv4.SetBase (subnet.str ().c_str (), "255.255.255.0");
      interfaceAdjacencyList[i] = ipv4.Assign (deviceAdjacencyList[i]);
    }

  //Turn on global static routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();


  // Create the OnOff applications to send TCP to the server
  // OnOffHelper clientHelper ("ns3::TcpSocketFactory", Address ());
  // clientHelper.SetAttribute 
  //   ("OnTime", RandomVariableValue (ConstantVariable (1)));
  // clientHelper.SetAttribute 
  //   ("OffTime", RandomVariableValue (ConstantVariable (0)));

  BulkSendHelper clientHelper ("ns3::TcpSocketFactory", Address ());
  // Set the amount of data to send in bytes.  Zero is unlimited.
  clientHelper.SetAttribute ("MaxBytes", UintegerValue (maxBytes));


  //normally wouldn't need a loop here but the server IP address is different
  //on each p2p subnet
  ApplicationContainer clientApps;
  ApplicationContainer sinkApps;
  uint16_t port = 50000;
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  PacketSinkHelper sinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
  for(uint32_t i=0; i<clientNodes.GetN (); ++i)
    {
      UniformVariable x (0,1);
      double rn = x.GetValue ();
      AddressValue remoteAddress
        (InetSocketAddress (interfaceAdjacencyList[i].GetAddress (1), port));
      clientHelper.SetAttribute ("Remote", remoteAddress);
      ApplicationContainer app = clientHelper.Install (clientNodes.Get (i));
      NS_LOG_INFO ((format("Application on device %d (%s) starting at %g") % (N+i+1)
                    % Names::FindName(clientNodes.Get (i))
                    % (AppStartTime+rn)).str());
      app.Start (Seconds (AppStartTime + rn));
      app.Stop (Seconds (AppStartTime + 10.0 + rn));
      clientApps.Add (app);
      sinkApps.Add (sinkHelper.Install (serverNodes.Get (i)));
    }

  sinkApps.Start (Seconds (0.01));
  sinkApps.Stop (Seconds (15.0));


  //configure tracing
  //AsciiTraceHelper ascii;
  //p2p.EnableAsciiAll (ascii.CreateFileStream ("tcp-qfp.tr"));
  PointToPointHelper pointToPoint;
  pointToPoint.EnablePcapAll ("tcp-qfp");

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Stop (Seconds (30.0));
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");

  return 0;
}
