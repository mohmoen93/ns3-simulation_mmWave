#include "ns3/mmwave-helper.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/config-store-module.h"
#include "ns3/command-line.h"
#include "ns3/mmwave-point-to-point-epc-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;
using namespace mmwave;

NS_LOG_COMPONENT_DEFINE ("Contoh Konsep TNM");
int
main (int argc, char *argv[])
{
  uint16_t numEnb = 1;
  uint16_t numUe = 3;
  double simTime = 1.0;
  double interPacketInterval = 100;
  double minDistance = 5.0; // eNB-UE distance in meters
  double maxDistance = 100.0; // eNB-UE distance in meters
  bool harqEnabled = true;
  bool rlcAmEnabled = false;
  uint32_t SentPackets = 0;
  uint32_t ReceivedPackets = 0;
  uint32_t LostPackets = 0;


  // Command line arguments
  CommandLine cmd;
  cmd.AddValue ("numEnb", "Number of eNBs", numEnb);
  cmd.AddValue ("numUe", "Number of UEs per eNB", numUe);
  cmd.AddValue ("simTime", "Total duration of the simulation [s])", simTime);
  cmd.AddValue ("interPacketInterval", "Inter-packet interval [us])", interPacketInterval);
  cmd.AddValue ("harq", "Enable Hybrid ARQ", harqEnabled);
  cmd.AddValue ("rlcAm", "Enable RLC-AM", rlcAmEnabled);
  cmd.Parse (argc, argv);

  Config::SetDefault ("ns3::MmWaveHelper::RlcAmEnabled", BooleanValue (rlcAmEnabled));
  Config::SetDefault ("ns3::MmWaveHelper::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::MmWaveFlexTtiMacScheduler::HarqEnabled", BooleanValue (harqEnabled));
  Config::SetDefault ("ns3::LteRlcAm::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));
  Config::SetDefault ("ns3::LteRlcUmLowLat::ReportBufferStatusTimer", TimeValue (MicroSeconds (100.0)));

  Ptr<MmWaveHelper> mmwaveHelper = CreateObject<MmWaveHelper> ();
  mmwaveHelper->SetSchedulerType ("ns3::MmWaveFlexTtiMacScheduler");
  Ptr<MmWavePointToPointEpcHelper>  epcHelper = CreateObject<MmWavePointToPointEpcHelper> ();
  mmwaveHelper->SetEpcHelper (epcHelper);
  mmwaveHelper->SetHarqEnabled (harqEnabled);

  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults ();

  // parse again so you can override default values from the command line
  cmd.Parse (argc, argv);

  Ptr<Node> pgw = epcHelper->GetPgwNode ();

  // Create a single RemoteHost
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);

  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (Seconds (0.010)));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (1);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  NodeContainer ueNodes;
  NodeContainer enbNodes;
  enbNodes.Create (numEnb);
  ueNodes.Create (numUe);


  // Install Mobility Model
  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator> ();
  enbPositionAlloc->Add (Vector (0.0, 0.0, 0.0));
  MobilityHelper enbmobility;
  enbmobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  enbmobility.SetPositionAllocator (enbPositionAlloc);
  enbmobility.Install (enbNodes);

  MobilityHelper uemobility;
  Ptr<ListPositionAllocator> uePositionAlloc = CreateObject<ListPositionAllocator> ();
  Ptr<UniformRandomVariable> distRv = CreateObject<UniformRandomVariable> ();
  for (unsigned i = 0; i < numUe; i++)
    {
      double dist = distRv->GetValue (minDistance, maxDistance);
      uePositionAlloc->Add (Vector (dist, 0.0, 0.0));
    }
  uemobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  uemobility.SetPositionAllocator (uePositionAlloc);
  uemobility.Install (ueNodes);

  // Install mmWave Devices to the nodes
  NetDeviceContainer enbmmWaveDevs = mmwaveHelper->InstallEnbDevice (enbNodes);
  NetDeviceContainer uemmWaveDevs = mmwaveHelper->InstallUeDevice (ueNodes);

  // Install the IP stack on the UEs
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (uemmWaveDevs));
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }

  mmwaveHelper->AttachToClosestEnb (uemmWaveDevs, enbmmWaveDevs);

  // install flowmon
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();

  // Install and start applications on UEs and remote host
  uint16_t dlPort = 1234;
  uint16_t ulPort = 2000;
  uint16_t otherPort = 3000;
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      ++ulPort;
      ++otherPort;
      PacketSinkHelper dlPacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), dlPort));
      PacketSinkHelper ulPacketSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), ulPort));
      PacketSinkHelper packetSinkHelper ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), otherPort));
      serverApps.Add (dlPacketSinkHelper.Install (ueNodes.Get (u)));
      serverApps.Add (ulPacketSinkHelper.Install (remoteHost));
      serverApps.Add (packetSinkHelper.Install (ueNodes.Get (u)));

      UdpClientHelper dlClient (ueIpIface.GetAddress (u), dlPort);
      dlClient.SetAttribute ("Interval", TimeValue (MicroSeconds (interPacketInterval)));
      dlClient.SetAttribute ("MaxPackets", UintegerValue (1000000));

      UdpClientHelper ulClient (remoteHostAddr, ulPort);
      ulClient.SetAttribute ("Interval", TimeValue (MicroSeconds (interPacketInterval)));
      ulClient.SetAttribute ("MaxPackets", UintegerValue (1000000));

//      UdpClientHelper client (ueIpIface.GetAddress (u), otherPort);
//      client.SetAttribute ("Interval", TimeValue (MilliSeconds(interPacketInterval)));
//      client.SetAttribute ("MaxPackets", UintegerValue(1000000));

      clientApps.Add (dlClient.Install (remoteHost));
      clientApps.Add (ulClient.Install (ueNodes.Get(u)));
//      if (u+1 < ueNodes.GetN ())
//        {
//          clientApps.Add (client.Install (ueNodes.Get(u+1)));
//        }
//      else
//        {
//          clientApps.Add (client.Install (ueNodes.Get(0)));
//        }
    }
  serverApps.Start (Seconds (0.1));
  clientApps.Start (Seconds (0.1));
  mmwaveHelper->EnableTraces ();
  // Uncomment to enable PCAP tracing
  // p2ph.EnablePcapAll ("mmwave-1");

  AnimationInterface anim("mmwave-1.xml");

  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();

  int j=0;
  float AvgThroughput = 0;
  Time Jitter;
  Time Delay;
  
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();
  
    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
      {
  	  Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);
  
  NS_LOG_UNCOND("----Flow ID:" <<iter->first);
  NS_LOG_UNCOND("Src Addr" <<t.sourceAddress << "Dst Addr "<< t.destinationAddress);
  NS_LOG_UNCOND("Sent Packets=" <<iter->second.txPackets);
  NS_LOG_UNCOND("Received Packets =" <<iter->second.rxPackets);
  NS_LOG_UNCOND("Lost Packets =" <<iter->second.txPackets-iter->second.rxPackets);
  NS_LOG_UNCOND("Packet delivery ratio =" <<iter->second.rxPackets*100/iter->second.txPackets << "%");
  NS_LOG_UNCOND("Packet loss ratio =" << (iter->second.txPackets-iter->second.rxPackets)*100/iter->second.txPackets << "%");
  NS_LOG_UNCOND("Delay =" <<iter->second.delaySum);
  NS_LOG_UNCOND("Jitter =" <<iter->second.jitterSum);
  NS_LOG_UNCOND("Throughput =" <<iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024<<"Kbps");
  
  SentPackets = SentPackets +(iter->second.txPackets);
  ReceivedPackets = ReceivedPackets + (iter->second.rxPackets);
  LostPackets = LostPackets + (iter->second.txPackets-iter->second.rxPackets);
  AvgThroughput = AvgThroughput + (iter->second.rxBytes * 8.0/(iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds())/1024);
  Delay = Delay + (iter->second.delaySum);
  Jitter = Jitter + (iter->second.jitterSum);
  
  j = j + 1;
  
  }
  
  AvgThroughput = AvgThroughput/j;
  NS_LOG_UNCOND("--------Total Results of the simulation----------"<<std::endl);
  NS_LOG_UNCOND("Total sent packets  =" << SentPackets);
  NS_LOG_UNCOND("Total Received Packets =" << ReceivedPackets);
  NS_LOG_UNCOND("Total Lost Packets =" << LostPackets);
  NS_LOG_UNCOND("Packet Loss ratio =" << ((LostPackets*100)/SentPackets)<< "%");
  NS_LOG_UNCOND("Packet delivery ratio =" << ((ReceivedPackets*100)/SentPackets)<< "%");
  NS_LOG_UNCOND("Average Throughput =" << AvgThroughput<< "Kbps");
  NS_LOG_UNCOND("End to End Delay =" << Delay);
  NS_LOG_UNCOND("End to End Jitter delay =" << Jitter);
  NS_LOG_UNCOND("Total Flod id " << j);
  // monitor->SerializeToXmlFile("manet-routing.xml", true, true);

  /*GtkConfigStore config;
  config.ConfigureAttributes();*/

  Simulator::Destroy ();
  return 0;

}
