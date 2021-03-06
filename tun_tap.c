#include <stdio.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#define ARP_ETHERNET    0x0001
#define ARP_IPV4        0x0800
#define ARP_REQUEST     0x0001
#define ARP_REPLY       0x0002
#define ETH_P_ARP	0x0806	
/*commands used for creating tap device 
sudo openvpn --mktun --dev tap1
sudo ip addr add 10.0.0.1/24 dev tap1
sudo ip link set tap1 up
-----------------------------------------------
We have created a virtual network interface (tun0 in my case), accessible by the current user 
and set it’s address to 10.0.0.1 If we run ip link we observe that the link is always DOWN:
Since there is no program attached to this virtual interface 
the kernel treats it like an Ethernet adapter without a cable connected (NO-CARRIER).
*/
/*
ethernet structure
*/
struct ethernet_header
{
    unsigned char dmac[6];
    unsigned char smac[6];
    uint16_t ethertype;///type of network layer protocol
    unsigned char payload[];
} __attribute__((packed));
//------------------------------------------------------------------------------------------------------------------------------------------
/*
arp header
*/
struct ARP_header
{
    uint16_t hardwareType; // This field specifies the network link protocol type. Example: Ethernet is 1.
    uint16_t protocolType; //is field specifies the internetwork protocol for which the ARP request is intended. For IPv4, this has the value 0x0800. 
    unsigned char hardwareSize; // Length (in octets) of a hardware address. Ethernet addresses size is 6(MAC).
    unsigned char protocolSize; // Length (in octets) of addresses used in the upper layer protocol. (The upper layer protocol specified in PTYPE.) IPv4 address size is 4
    uint16_t operation; //Specifies the operation that the sender is performing: 1 for request, 2 for reply.
    unsigned char data[]; // payload of arp message in our case, this will contain IPv4 specific information
} __attribute__((packed));
//-----------------------------------------------------------------------------------------------------------------------------------
/*
The data field contains the actual payload of the ARP message, and in our case, this will contain IPv4 specific information:
*/
struct arp_ipv4
{
    unsigned char smac[6]; //Sender hardware address (SHA)
    uint32_t sip;// Internetwork address of the sender.
    unsigned char dmac[6];// Target hardware address (THA)
    uint32_t dip; // Internetwork address of the destination.
} __attribute__((packed));
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------
/*
The TUN/TAP driver is a way for userspace applications to do networking. It creates a virtual network interface that behaves like a real one but every packet it receives gets forwarded to a userspace application. TUN adapters operate with layer 3 packets (such as IP packets) and TAP adapter works with layer 2 packets (like Ethernet frames). TUN is therefore useful for creating a point-to-point network tunnel while TAP can bridge two networks like a switch (but at a slightly higher overhead).
*/
static int tun_alloc(char *dev)
{ ///struct for linux network contain interface name and another set of variables like address and so on
    struct ifreq interface_data;
    int file_descriptor, err;
///first step open device 
    if( (file_descriptor = open("/dev/net/tun", O_RDWR)) < 0 ) {
        perror("Cannot open TUN/TAP dev\n");
        exit(1);
    }

    /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
     *        IFF_TAP   - TAP device
     *
     *        IFF_NO_PI - Do not provide packet information
     */
     
    memset(&interface_data, 0, sizeof(interface_data));
    interface_data.ifr_flags = IFF_TAP | IFF_NO_PI;
    if( *dev ) {
/// copy name that i give it to the device in struct ifreq 
///IFNAMSIZ --> size of name that declared in struct 
        strncpy(interface_data.ifr_name, dev, IFNAMSIZ);
    }
///check if there is an error in creating interface
/*TUNSETIFF constant:
If a non-existent or empty interface name is specified, the kernel will create a new interface if the user has the CAP_NET_ADMIN permission (or is root).
*/
    if( (err = ioctl(file_descriptor, TUNSETIFF, (void *) &interface_data)) < 0 ){
        perror("Error: Could not ioctl tun");
        close(file_descriptor);
        return err;
    }

    strcpy(dev, interface_data.ifr_name);
    return file_descriptor;
}
/*read and write to interface:
Reading and writing to the virtual interface is simple, we can use read() and write() like any file.
*/
///handle reading from interface
///return value (0,1) if 1 means successful read 
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------
int read_from_interface(int file_descriptor,char *buffer, int buffer_length){
 int return_value = read(file_descriptor, buffer, buffer_length);
 if(return_value<0) return -1;
 else
  return return_value ;
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------------
int tun_write(int file_descriptor,char *buffer, int buffer_length)
{
    return write(file_descriptor, buffer, buffer_length);
}
//check the type of network layer packet 
//---------------------------------------------------------------------------------------------------------------------------------------------
int check_type_of_network_layer(struct ethernet_header *hdr){
   if(hdr->ethertype==ETH_P_ARP)
     return 0;
   else if(hdr-> ethertype == ETH_P_IP)
     return 1;
   else 
     return -1;
}
//--------------------------------------------------------------------------------------------------------------------------------------------------------
void handle_arp_request(struct ARP_header *arp,struct arp_ipv4 *arpPayload,struct ethernet_header *header,int file_descriptor,int read_value){
    memcpy(arpPayload->dmac, arpPayload->smac, 6);
    uint32_t x =  arpPayload->sip;
    arpPayload->sip = arpPayload->dip;
    arpPayload->dip = arpPayload->sip;
    memset(arpPayload->smac, 0, 6);
    arp->operation = ARP_REPLY;
    arp->operation = htons(arp->operation);
    arp->hardwareType = htons(arp->hardwareType);
    arp->protocolType = htons(arp->protocolType);
    arpPayload->sip = htonl(arpPayload->sip);
    arpPayload->dip = htonl(arpPayload->dip);
    memcpy(header->dmac, arpPayload->dmac, 6);
    memcpy(header->smac, arpPayload->smac,6);
     for(int i=0;i<6;i++){
        printf("%.2hhx:",arpPayload->smac[i]);
      }
    header->ethertype = htons(ETH_P_ARP);
    printf("writing %d\n",write(file_descriptor, (char*)header, read_value));
   
    
}
//--------------------------------------------------------------------------------------------------------------------------------------------------------
void check_reply_or_request(struct ARP_header *arp,struct ethernet_header *header,int file_descriptor,int read_value)
{
printf("%4hx",arp->operation);

if(arp->operation == ARP_REQUEST && arp ->protocolType == ARP_IPV4)
{ 
  struct arp_ipv4 *arppayload = (struct arp_ipv4*)arp->data;
  arppayload ->sip = ntohl(arppayload->sip);
  arppayload ->dip = ntohl(arppayload->dip);
  handle_arp_request(arp,arppayload,header,file_descriptor,read_value);
}
else 
 printf("Not supported\n");
}
//-----------------------------------------------------------------------------------------------------------------------------------------------------------
void parse_arp_format(struct ethernet_header *header,int file_descriptor,int read_value){
struct ARP_header *arp = (struct ARP_header *) (header->payload);
arp->operation = ntohs(arp->operation);
arp->protocolType = ntohs(arp->protocolType);

check_reply_or_request(arp,header,file_descriptor, read_value);

}
int main(){

  char tun_name[IFNAMSIZ];
  strcpy(tun_name, "tap1");
  int file_descriptor = tun_alloc(tun_name);  
  if(file_descriptor < 0){
   printf("error in Allocating interface\n");
   return 0;
  }
while(1) {
  /* Now read data coming from the kernel */
  /* Note that "buffer" should be at least the MTU size of the interface, eg 1500 bytes */
  char buffer[1000];
  int read_value = read_from_interface(file_descriptor,buffer,sizeof(buffer));
  if(read_value == -1) {
    printf("error\n");
    close(file_descriptor);
    return 0;
  }
  //parsing ethernet package format 
  printf("Read %d bytes from device %s %s\n", read_value, tun_name,buffer);
  struct ethernet_header *header =  (struct ethernet_header *)(buffer); 
  header->ethertype = ntohs(header->ethertype) ;
  if(check_type_of_network_layer(header) == 0)
    parse_arp_format(header,file_descriptor,read_value);
  else 
    printf("Not supported");

 }
  return 0;
}
