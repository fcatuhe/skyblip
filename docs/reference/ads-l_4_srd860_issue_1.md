```
           Technical Specification
                                       for
     ADS-L transmissions
using SRD-860 frequency band
                 (ADS-L 4 SRD-860)
```

ACCEPTABLE METHODS, TECHNIQUES AND PRACTICES FOR CARRYING OUT ADS-L TRANSMISSIONS USING SRD-860 FREQUENCY BAND AS PERMITTED PURSUANT TO AMC1 SERA.6005(c) POINT (a)(3)(i)

```
        Issue 1
20 December 20221
```

1 For the date of entry into force of this Issue, please refer to Decision 2022/024/R at the Official Publication of EASA.

## TABLE OF CONTENTS

Note: To support the identification of improvements to ADS-L 4 SRD-860, as well as its future evolution, EASA would appreciate stakeholders’ voluntary feedback through the EASA website2.

2 https://www.easa.europa.eu/easa-and-you/general-aviation

## PREAMBLE

```
                              ED Decision 2022/024/R

ADS-L 4 SRD-860 Issue 1
```

The following is a list of paragraphs affected by this Issue: Whole document

```
Issue 1                                                           New (NPA 2021-14)

                                                                      ED Decision 2022/024/R
```

## DEFINITIONS AND ABBREVIATIONS

```
Term                      Meaning/Explanation
°                         Decimal degree, unit of angle
3D                        3 dimensional
0x                        Hexadecimal number notation
ADS-L                     Automatic Dependent Surveillance – Light
AGL                       Above Ground Level
ANSI                      American National Standards Institute
bps                       Bits per second
CA                        Certificate Authority
CEP                       Circular Error Probable, a common term for GNSS accuracy, 50%
                          confidence
COTS                      Commercially available off-the-shelf products
CRC                       Cyclic redundancy check
CSMA                      Carrier-sense multiple access
CTA                       Consumer Technology Association
DAA                       Capability to see, sense or detect conflicting traffic or other hazards
                          and take the appropriate action
Drone                     UAS
EASA                      European Aviation Safety Agency
e-Conspicuity             Electronic conspicuousness of manned aircraft
EdDSA                     Edwards-curve Digital Signature Algorithm
EGM96                     Earth Gravitational Model 1996
eID                       Electronic Identification
ERP                       Effective radiated power
ETSO                      European Technical Standard Order
FAA                       US Federal Aviation Administration
GCS                       Ground Control Station (for UAS)
GFSK                      Gaussian frequency shift keying
GNSS                      Global Navigation Satellite System, such as GPS
IETF / IRTF               Internet Engineering/Research Task Force
IMZ                       eID mandatory zones
ISO                       International Organization for Standardization
ITU                       International Telecommunication Union
JARUS                     Joint Authorities for Rulemaking on Unmanned Systems
LAANC                     Low Altitude Authorization and Notification Capability
LBT                       Listen before talk
LUT                       Look-up table
LSB                       Least significant bit
m                         Meter, unit of distance, also vertically
MAC                       Media access control
MOPS                      Minimum Operational Performance Standards
MSL                       Mean Sea Level, as used in EGM96
NMEA                      National marine electronics association
Operator                  An entity or individual under which UAS and pilots operate
OSI                       Open Systems Interconnection
Pilot                     A human individual remotely controlling or flying a UAS
R/C                       Remotely controlled
RF                        Radio frequency
RPAS                      Remotely Piloted Aircraft Systems, the regulator’s current term for
                          UAS
s                         Second, unit of time
SoC                       System on chip

SORA                    Specific Operations Risk Assessment
SRD                     Short-Range Device, a term from CEPT/ECC Recommendation 70-03
SSL                     Secure Sockets Layer
TRNG                    True Random Number Generator
UAS                     Unmanned Aircraft System, the UAV and connected ground-station
UAV                     Unmanned Aerial Vehicle, also known as a drone, multicopter, RPAS
                        or R/C model aircraft; may carry manned cargo
UTC                     Coordinated Universal Time
UTM / UTMS              UAS Traffic Management System
V2V                     Vehicle to vehicle communications
V2X                     Vehicle to infrastructure communications
VLOS                    Visual line-of-sight
WGS84                   World geodetic system 1984
```

## SUBPART A - GENERAL

### ADS-L.4.SRD860.A.1 Introduction

This document is the initial technical specification of ADS-L transmissions using SDR860 frequency band for aircraft to become electronically conspicuous to U-space Service Providers (USSPs). It is intended for manufacturers interested to develop ADS-L compliant e-Conspicuity device/system.

### ADS-L.4.SRD860.A.2 Background

On April 21st 2021, the Commission Implementing Regulation (EU) 2021/666 was published, amending SERA.6005 by the sub-article (c):

```
    Manned aircraft operating in airspace designated by the competent
authority as a U-space airspace, and not provided with an air traffic control
  service by the ANSP, shall continuously make themselves electronically
               conspicuous to the U-space service providers.
```

On December 16, 2021, EASA further published NPA 2021-14: “Development of acceptable means of compliance and guidance material to support the U-space regulation” (“AMC/GM”). Section 3.3 introduced in more detail the AMC and GM for implementing above SERA.6005 (c) article. AMC1 SERA.6005(c) in point (a)(3)(ii) specifies SRD-860 frequency band as one of the three means of transmission of electronic conspicuity information. Appendix 1 to AMC1 SERA.6005(c) further specifies ADS-L MESSAGE GENERATION FUNCTION i.e. the minimum set of parameters that should be transmitted and a set of parameters that may be transmitted optionally, termed “ADS-L” thereafter.

### ADS-L.4.SRD860.A.3 Scope

The technical specification is intended to be a complete and accurate description of ADS-L, including physical layer, timing details, data semantics, byte packaging and so forth. It does not cover other aspects of a practical system for electronic conspicuity, such as: User interface, configuration, error handling, software upgrades and so forth. It is open to any interested party for implementation. In some of the definitions, C code snippets are used. These snippets are intended to provide easy-to-adopt examples. The snippets are hardware-agnostic and fully portably. The code follows the ANSI C99 standard and assume a standard integer width of 32 bits. More formal descriptions will be added later.

### ADS-L.4.SRD860.A.4 Qualification System

There is no ETSO or MOPS for ADS-L at the time of writing of this specification. Responsibility for the compliance with SERA.6005(is ultimately with the operator of the aircraft in which an ADS-L system is being operated. A statement of “Non Technical Objection (NTO)” may optionally be obtained by the manufacturers from EASA to confirm to aircraft operators that the device/system is credible for this intended function in compliance with SERA.6005(c).

## SUBPART B - OVERVIEW

### ADS-L.4.SRD860.B.1 Block Diagram

A typical implementation block diagram is given below. The implementation consists of at least:

```
●       GNSS receiver for 3D localization and timing,

●       Host processor

●       RF frontend, capable of transmitting RF signals.
```

In this setup, the processing of the eID protocol happens in the host processor. Alternative setups use an SoC for RF and protocol processing in one package, or implement a standalone eID transmitter.

```
                    Host
GNSS                                       RF
                  Processor
```

In addition to the specifications given in here, all transmissions must comply with local regulations.

### ADS-L.4.SRD860.B.2 Protocol Architecture

ADS-L is a lightweight, stateless, undirected broadcast protocol. It loosely follows the ISO/IEC 7498-1 OSI model using layers 1, 2, and 6 (see below, red).

```
Layer 7: Application

Layer 6: Presentation

Layer 5: Session

Layer 4: Transport

Layer 3: Network

Layer 2: Data Link

Layer 1: Physical
```

### ADS-L.4.SRD860.B.3 Bit and Byte Order

The following principles apply unless noted otherwise.

```
   Byte size: A byte consists of 8 bits. The least significant bit has a weight of 0x01, while the most
    significant bit has a weight of 0x80 (128 decimal).

   Byte order: Little Endian byte order is used throughout. That is, integers are transmitted on the
    RF least significant byte first. The data is assumed to be stored in the memory in the same order.

   Bit order: Individual bytes are transmitted most significant bit first.
```

Example: The integer 1328922 (0x14471A) is transmitted as 0x1A, 0x47, 0x14 on the radio (from left to right). The exact bit sequence on the RF is: 00011010, 01000111, 00010100 (again left to right).Note: SC-CS005() refers to the installation of ADS-B OUT equipment.

### ADS-L.4.SRD860.B.4 Transmission Sequence

All data is sent in discrete packets, the size of which is determined by the payload size. The structure of a packet is indicated below.

```
Preamble         Sync Word          Payload           CRC         Signature

                                                                                 time
```

Note the signature (blue) is optional; its presence must be indicated in the header (see Subpart D). The payload is further structured by the various layers of the ADS-L protocol.

### ADS-L.4.SRD860.B.4 Future Development

The protocol may evolve and change in future. Implementations shall always be consistent with the standard version designated in the “Protocol version” field in the messages. Once new versions become available, old versions will be deprecated during a phase-over period. Implementers shall switch to the latest version in reasonable time.

## SUBPART C - PHYSICAL LAYER

### ADS-L.4.SRD860.C.1 Introduction

Two physical systems are supported by ADS-L: M-band3 operating in the 868. .. 868.6 MHz spectrum, and O-band operating in the 869.4 .. 869.65 spectrum. These band differ substantially in allowable duty cycle and power, as well as the expected level of interference. Both bands also have a different set of legacy systems operating therein, whose modulation parameters are to be adopted by ADS-L as best as possible. Different modulation schemes are thus employed for each band.

### ADS-L.4.SRD860.C.2 M-Band

A digital modulation scheme (2-GFSK) is employed to transmit data. The key parameters are given in the table below.

```
Frequency                                          868.2 MHz, 868.4 MHz

Channel Bandwidth                                  200 kHz

Modulation                                         GFSK

Max. Power (ERP)                                   14 dBm / 25 mW

Chip rate                                          100 kbps
                                                   Effective bitrate is halved due to Manchester
                                                   encoding

Frequency Deviation                                0: -50 kHz, 1: +50 kHz

Gauss Filter BT                                    0.5

Backoff Interval (see Subpart D)                   15 ms .. 250 ms
```

The frequencies of the M-band are used: 868.2 and 868.4 MHz. An ADS-L sender shall alternate between these frequencies for every transmission to allow better usage of the frequency spectrum.

#### ADS-L.4.SRD860.C.2.1 Manchester Encoding

Manchester encoding is applied to the Sync Word, Payload, and CRC for better clock synchronization (lower error rate and more reliable transmission), halving the effective bit rate to 50kbps. Manchester encoding is not applied to the Preamble, see below. The encoding is such that a ‘1’ is encoded as ‘01’ and a ‘0’ as ‘10’.

If the (optional) secure signature is used, Manchester encoding is disabled for transmitting it to enable a shorter on-air time. This means the transmitter shall switch to non-Manchester mode after transmitting the CRC. See the next section on the ADS-L packet structure.

3 See EN 300 220-2 for a definition of both bands

#### ADS-L.4.SRD860.C.2.3 Preamble

The Preamble is sent by the transmitter to allow eligible receivers to synchronize their clocks. It consists of two parts:

```
1.       A sequence of zeroes and ones of at least length 10 symbols, maximum 24 symbols. The
         sequence shall end with a 1, and
2.       the sequence 1001, transmitted twice.
```

Manchester encoding is not applied to the Preamble. The shortest valid preamble thus is (sent from left to right): 01 0101 0101 1001 1001.

#### ADS-L.4.SRD860.C.2.4 Sync Word

The Sync Word is 2 bytes, each transmitted with the most significant bit first. The Sync Word content is given below as arrays, transmitted left to right. Manchester encoding is to be applied to the Sync Word before sending.

```
Hex                                                 0x72 0x4B

Binary                                              01110010 01001011
```

### ADS-L.4.SRD860.C.3 O-Band

A digital modulation scheme (2-GFSK) is employed to transmit data. The key parameters are given in the table below.

```
Frequency                                             869.525 MHz

Channel Bandwidth                                     250 kHz

Modulation                                            GFSK

Max. Power (ERP)                                      27d Bm / 500 mW

Chip rate                                             38.4 kbps

Frequency Deviation                                   0: -10 kHz, 1: +10k Hz

Backoff Interval (see Subpart D)                      10 ms .. 50 ms
```

#### ADS-L.4.SRD860.C.3.2 Preamble

The Preamble is sent by the transmitter to allow eligible receivers to synchronize their clocks. It consists of a sequence of 10 bytes 0xAA

#### ADS-L.4.SRD860.C.3.1 Sync Word

The Sync Word is 2 bytes, each transmitted with the most significant bit first. The Sync Word content is given below as arrays, transmitted left to right.

```
Hex       0x2D 0xD4
```

Binary 0010 1101 1101 0100

```
Hex                                               0x2D 0xD4

Binary                                            0010 1101 1101 0100
```

## SUBPART D - DATA LINK LAYER

### ADS-L.4.SRD860.D.1 Data Link Frame Structure

The data link layer governs the following aspects of the protocol:  Mediating access to the RF channel for maximum scalability and fairness  Ensuring data integrity by means of adding a cyclic redundancy check.  Optionally, authenticating the message by adding a secure signature

The structure of the data link frame is given as:

```
Offset                        Description       Width (bits)      Encoding

0                             Packet Length     8                 Length of the packet in bytes,
                                                                  excluding this field and
                                                                  excluding the signature (if
                                                                  present)

8                             Data              (Packet           See Subpart F
                                                Length - 3) *
                                                8

(Packet Length + 1) *         CRC               24                Redundancy check
```

8

```
(Packet Length + 3) *         Secure            512               Optional
8                             Signature
```

#### ADS-L.4.SRD860.D.1.1 Packet Length

This field contains the number of bytes of the full message including the header, payload, and CRC, but excluding the length field itself, and the signature. Some payload types have variable lengths, in which case this field must be consulted to determine the true payload length.

#### ADS-L.4.SRD860.D.1.2 CRC

A 24-bit cyclic redundancy check (CRC) for message integrity is transmitted after the Payload. The CRC is to be computed over the entire ADS-L packet, excluding the Packet Length field and the Secure Signature. The CRC code is identical to the one used in Mode-S and 1090 MHz ADS-B. The generator polynomial is thus given by:

```
G(x) = x24 + x23 + x22 + x21 + x20 + x19 + x18 + x17 +
      x16 + x15 + x14 + x13 + x12 + x10 + x3 + 1
```

A sample implementation is given below: static uint32_t _crc24_polypass(uint32_t crc, uint8_t input) { const uint32_t poly = 0xFFFA0480;

crc |= input; for (uint8_t bit = 0; bit < 8; bit++) { if (crc & 0x80000000)

```
crc ^= poly;
```

crc <<= 1; } return crc; }

uint32_t crc24(const uint8_t *data, size_t len_data) { uint32_t crc = 0; for (size_t idx = 0; idx < len_data; idx++) { crc = _crc24_polypass(crc, data[idx]); } crc = _crc24_polypass(crc, 0); crc = _crc24_polypass(crc, 0); crc = _crc24_polypass(crc, 0); return crc >> 8; }

Note that only the 3 lower bytes of the result are relevant and to be transmitted on the RF in Big endian, i.e. most significant byte first.

#### ADS-L.4.SRD860.D.1.3 Secure Signature

Implementing devices may optionally digitally sign the transmitted data packets using a secure public-key digital signature algorithm to improve the trustworthiness of their transmission. The private key shall be stored securely on the device and be protected against all reading. Only the associated public key may ever be exposed. A device may implement automatic creation of a new public key, e.g. by means of a true random number generator. The digital signature links a physical device, represented by its secret private key, to a real entity (legal or natural). This relationship is typically stored in a certificate authority (CA), operated e.g. by the competent authority. CA and other infrastructure are outside the scope of this document. The secure signature is optionally appended to the payloads after the message checksum (CRC). Simple receivers may thus only receive messages while ignoring the signature. The signature is optional. If used, it shall be appended at most every 10 seconds, at least every 30 seconds. The algorithm is the Edwards-curve Digital Signature Algorithm (EdDSA), specifically the Ed25519 variant as described in IRTF/IETF RFC 8032. The public key is 256 bits, the signature is 512bits in compressed form 4 . The algorithm takes the ADS-L Messages including header and payload as input, but excluding the CRC. If message scrambling / encryption is used, then the input shall be used after encryption.

4 IRTF RFC 8032, https://datatracker.ietf.org/doc/html/rfc8032

### ADS-L.4.SRD860.D.3 Transmit Rate and Transmit Timing

The standard message transmit rate is once every second whenever. Devices may optionally transmit with less frequency based on the pattern of motion, the spectrum usage, or energy considerations. The minimum nominal transmission rate is once per 5 seconds. Data packets shall be discarded if the total delay of 1000ms from obtaining the GNSS fix is exceeded, e.g. due to the frequency being occupied, to prevent old data being sent.

### ADS-L.4.SRD860.D.4 Media Access

The MAC protocol to be used is 1-persistent Carrier-Sense Multiple Access (CSMA). Listen-before-talk (LBT) must be implemented according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1. Devices must sense for an existing carrier before transmitting. If a carrier is detected, transmission of the packet shall be delayed (backoff). Devices shall retry transmission after a random delay, distributed uniformly in the backoff interval as defined in Subpart C, depending on the band in use. If no packet can be transmitted 3000 ms after the initial attempt, the device may force transmission irrespective of carrier detect. After a forced transmission, the device may not transmit for at least 2000 ms.

## SUBPART E - NETWORK LAYER

### ADS-L.4.SRD860.E.1 Network Packet Structure

The network layer governs the following aspects of the ADS-L protocol:  A payload length field is included for easy interoperability with inexpensive, discrete FSK transceiver systems  Protocol versioning, such that newer versions of the protocol can be safely deployed  The addition of the secure signature is indicated here  Data scrambling, improving the resilience to transmission errors.

The network packet structure is given as:

```
Offset                        Description         Width (bits)        Encoding

0                             Protocol            4
                              Version

4                             Secure              1                   1 indicates presence of the
                              Signature flag                          secure signature

5                             Key Index           2                   Designates the key used, 0 for
                                                                      public

7                             Reserved            1                   Set to 0

8                             Data                (Packet             See Subpart F
                                                  Length - 3) *
                                                  8
```

The individual fields are further explained below.

#### ADS-L.4.SRD860.E.1.1 Protocol Version

Shall be set to 0 to indicate this version of the ADS-L protocol specification is being used for transmission. Future versions of ADS-L shall keep the position of this field relative to the ADS-L packet constant such that a receiver shall always first check this field to determine further processing and parsing.

#### ADS-L.4.SRD860.E.1.2 Secure Signature Flag

If set to 1, the Secure Signature shall be added after the CRC, see above.

#### ADS-L.4.SRD860.E.1.3 Key Index

This field indicates with key is used for the encryption / scrambling of the header and payload. By default, key 0 is used and given by:

```
key0                                                         0x00000000
```

Key indices 1 through 3 are reserved.

### ADS-L.4.SRD860.E.2 Encryption / Data Scrambling

The data portion is encrypted to ensure message integrity, system safety and provide protection for the relevant content against eavesdropping, namely by article 3 of the Budapest Convention on Cybercrime and Directive 2013/40/EU. The XXTEA symmetric encryption algorithm with 6 rounds shall be used. The algorithmic network of the cipher is given below. An example C implementation is also given below. The required parameters are indicated in the table below.

The part of the packet to be scrambled/encrypted with XXTEA shall be a multiple of four bytes as XXTEA works on 32-bit words. If only for data scrambling, the XXTEA can be calculated with encryption key being all-zeros (this simplifies the code and has been checked to work) and the more fields are taken for it the more protection there is for those. Scrambling enables an additional verification check as the scrambled packet content is sensitive to single bit errors which become immediately noticeable and they make the packet look pseudo-random. noticeable and they make the packet look pseudo-random.

```
#define MX       ((((z>>5)^(y<<2))+((y>>3)^(z<<4)))^((sum^y)+(key[(p&3)^e]^z)) )
```

void xxtea_encrypt(uint32_t* data,

```
uint32_t num_data_words,
const uint32_t* key,
uint32_t round)
```

{

uint32_t z, y = data[0], sum = 0, e, DELTA = 0x9e3779b9; uint32_t p, q; z = data[num_data_words - 1]; q = round; while (q-- > 0) { sum += DELTA; e = sum >> 2 & 3; for (p = 0; p < n - 1; p++) {

```
y = data[p + 1];
data[p] += MX;
z = data[p];
```

} y = data[0]; data[num_data_words - 1] += MX; z = data[num_data_words - 1]; } }

```
Encryption Parameter                                    Value

Round                                                   6

num_data_words                                          6
```

## SUBPART F - PRESENTATION LAYER

### ADS-L.4.SRD860.F.1 ADS-L Data

The Presentation Layer builds on top of the Network Layer, adding the concrete ADS-L functions. It identifies the sender and provides a means to enumerate the concrete payload type. The data layout of the ADS-L data is given as:

```
Offset           Description              Width (bits)                  Encoding

0                ADS-L Header             40                            See below

40               ADS-L Payload            Variable, must be             Variable
                                          multiple of 32
```

Currently, only the iConspicuity5 payload type is described in this document. As ADS-L aims to be extensible, further payload types may be added in the future.

### ADS-L.4.SRD860.F.2 ADS-L Header

```
Offset           Description              Width (bits)                 Encoding

0                Payload Type             8                            Payload type discriminator, see
                 Identifier                                            below

8                Sender Address           30                           Unique address of the sender,
                                                                       see below

38               Reserved                 1                            Shall be set to 0

39               Relay/forward            1                            This packet has been
                                                                       retransmitted/relayed/forwarded
                                                                       on behalf of the sender above
```

#### ADS-L.4.SRD860.F.2.1 Payload Type Identifier

This field defines the structure of the payload field. The upper half of the available range is reserved for unicast payloads, implying a specific structure of the payload, see below.

```
   Value        Transmission Type                Payload

   0            Broadcast                        Reserved

   1            Broadcast                        Reserved

2               Broadcast                  iConspicuity

3               Broadcast                  Reserved

…               Broadcast                  …

127             Broadcast                  Reserved

128             Unicast                    Reserved

129             Unicast                    Reserved

..              Unicast                    …

255             Unicast                    Reserved
```

5 EASA safety concept described as: ‘In-flight capability’ to transmit position and/or to receive, process and display information about other aircraft, airspace, weather or support to navigation in a real time with the objective to enhance pilots’ situational awareness

#### ADS-L.4.SRD860.F.2.2 Address

```
Offset       Description                          Width (bits)

0            Address Mapping Table                6

6            Address                              24

Address Mapping Table (AMT)                                       Value

Random / Privacy                                                  0

Reserved                                                          1..3

Reserved                                                          4

ICAO                                                              5

FLARM and OEMs                                                    6

OGN-Tracker                                                       7

FANET and OEMs                                                    8

Manufacturers Page 0                                              9

Manufacturers Page 1                                              10

…                                                                 …

Manufacturers Page 54                                             63
```

Entries 8 and higher of the address mapping table imply a further structuring of the address:

```
Offset       Description                            Width (bits)

0            Manufacturer prefix                    8

8            Base address                           16
```

Manufacturers intending to implement ADS-L shall apply for a unique manufacturer prefix, associated to one of the manufacturer pages of the AMT. The combination AMT page + manufacturer prefix shall be uniquely assigned to only one manufacturer. The manufacturer shall use the remaining 16-bit base address space as densely as possible. Once the range is used up, the manufacturer shall apply for a new AMT page + manufacturer prefix combination to use.

#### ADS-L.4.SRD860.F.2.3 Privacy Mode

In order to not disclose its identity, a device may be operated in privacy mode. In this mode, the address is selected randomly. Privacy mode is activated by selecting entry 0 of the AMT. The sender shall randomly and uniformly select manufacturer prefix and base address at device start-up. This random address shall not change while the device is operated. Toggling between privacy and normal modes during flight is not admissible.

#### ADS-L.4.SRD860.F.2.4 Payload

The payload structure and semantics are defined by the Payload Type Identifier. Individual payloads are defined below. Any new payload (i.e. not defined herein) must be approved by the governing body before using it. Any new payload shall be of a size appropriate for encryption, i.e. 5 + (payload size in bytes) must be divisible by four. Valid payload size thus are: 3, 7, 11, 15, 19, 23, 27.

## SUBPART G - APPLICATION LAYER

### ADS-L.4.SRD860.G.1 iConspicuity Payload

The iConspicuity payload is designed to comply with AMC1 SERA.6005(c) as proposed in EASA NPA 2021-14. For easier readability, related data are grouped together in line with the “Data Type” column in the NPA. The bit offset in the following tables always refer to the start of the payload. The structure of the payload is given by:

```
Offset              Description                   Width (bits)    Encoding

0                   Timestamp (of                 6               ¼ second
                    navigation solution)

6                   Flight state                  2               Table

8                   Aircraft category             5               Table

13                  Emergency status              3               Table

16                  Latitude                      24              LSB = 1° / 93206, North
                                                                  positive

40                  Longitude                     24              LSB = 1° / 46603, East positive

64                  Ground speed                  8               exp. encoding
                                                                  0..240 m/s, 0.25m/s step at
                                                                  lower range

72                  Altitude above WGS-           14              exp. encoding,
                    84 ellipsoid                                  -316m .. +61000m
                                                                  1m step

86                  Vertical rate                 9               exp. encoding
                                                                  +/-120 m/s, 0.125m/s step
                                                                  special value for “absent”

95                  Ground track                  9               cyclic: 1bit = 360/512 = 0.7deg

104                 Source integrity level        2               Table

106                 Design assurance              2               Table

108                 Navigation integrity          4               Table

112                 Horizontal position           3               Table
                    accuracy

115                 Vertical position             2               Table
                    accuracy

117                 Velocity accuracy             2               Table

119                 Reserved                      1               Table
```

The individual fields are further explained in the following Sections.

#### ADS-L.4.SRD860.G.1.1 Timestamp

Timestamp indicating quarter seconds since the full hour, modulo 60. The values 60..63 shall not be used. The timestamp refers to the time of the navigation solution used for the iConspicuity payload. Usually this is when the GNSS receiver made its measurement. The actual transmission of the radio packet can be later due to processing, media access wait times or excess spectrum usage.

#### ADS-L.4.SRD860.G.1.2 Flight State

```
Flight State                                                      Value

Undefined                                                         0

On ground                                                         1

Airborne                                                          2

Reserved                                                          3
```

#### ADS-L.4.SRD860.G.1.3 Aircraft Category

```
Aircraft Category                                                     Value

No emitter category information available                             0

Light fixed wing (< 7031 kg / 15 500 lbs)                             1

Small to heavy fixed wing (≥ 7031 kg / 15 500 lbs)                    2

Rotorcraft                                                            3

Glider / sailplane                                                    4

Lighter-than-air                                                      5

Ultralight                                                            6

Hang-glider / paraglider                                              7

Parachutist / skydiver / wingsuit                                     8

eVTOL / UAM                                                           9

Gyrocopter                                                            10

UAS Open category                                                     11

Aircraft Category                                                      Value

UAS Specific category                                                  12

UAS Certified category                                                 13

Reserved                                                               14

…                                                                      …

Reserved                                                               31
```

#### ADS-L.4.SRD860.G.1.4 Emergency Status

```
Emergency Status                                                   Value

Undefined                                                          0

No emergency                                                       1

General emergency                                                  2

Lifeguard/medical emergency                                        3

No communications                                                  4

Unlawful interference                                              5

Downed aircraft                                                    6

Reserved                                                           7
```

#### ADS-L.4.SRD860.G.1.5 Latitude and Longitude

The WGS-84 reference system is used throughout. The position expressed in degrees latitude and longitude, respectively. Both components are encoded as 24-bit signed integers. The least significant bit for the latitude is 1° / 93206; for longitude 1° / 46603. North and East are positive, respectively. A special value of 0xFFFFFF is used to indicate a lack of a position fix.

#### ADS-L.4.SRD860.G.1.6 Exponential Encoding

The following fields use exponential encoding with varying bit sizes. The two leading bits of the encoded value are used as a scaling exponent, allowing for a much larger value range to be encoded, while keeping high accuracy around zero. Exponential encoding is used for unsigned (altitude, ground speed) and signed (vertical rate) fields. For unsigned fields, the exponent is shifted one bit to the right, using the (now free) leading bit as a sign bit. The layout of an encoded unsigned value thus is:

```
<exponent:2> <base:N>
```

Where the number after the colon indicates the number of bits occupied by this field. For signed values, the layout becomes:

```
<sign:1> <exponent:2> <base:N>
```

Note that N is derived from the total width of the field, e.g. N=6 for Ground speed and N=6 for Vertical rate. Given an unsigned encoded value consisting of exponent and base, the decoded value is given by

```
value = 2exponent*(2N + base) – 2N
```

For signed encoded values, the sign field shall indicate that the decoded value is negative, i.e. the above result multiplied by -1. For encoding unsigned values, the following algorithm shall be used: 1. Find the largest e* from the set (0, 1, 2, 3) such that value >= 2N+e* – 2N 2. Return exponent = e*, base = value - 2N+e* – 2N For encoding signed values, the sign bit shall be set if value is negative. Exponent and base shall be computed as above using abs(value) as input. A value of 0 shall always be encoded as non-negative, i.e. the sign bit shall be cleared. In addition to the exponential encoding, the fields use a scaling factor and – the altitude – an offset. These parameters are indicated below.

#### ADS-L.4.SRD860.G.1.7 Altitude above WGS-84 Ellipsoid

The resolution of the encoding is 1m. An offset of 320m is added to encode negative altitudes. Exponential (unsigned) encoding is used with 2 bits for the exponent and 12 bits for the base. The minimum encodable altitude is -320 m, the maximum is 61112 m. Examples:

```
Altitude                                                             Encoded Field (14 bits)

-320 m                                                               0x0000

0m                                                                   0x0140

1000 m                                                               0x0528

61112 m                                                              0x3fff
```

#### ADS-L.4.SRD860.G.1.8 Ground Speed

The resolution for encoding ground speed is 0.25 m/s. Exponential (unsigned) encoding is used with 2 bits for the exponent and 6 bits for the base. The maximum encodable ground speed is 238 m/s. Examples:

```
Ground Speed                                                       Encoded Field (8 bits)

0.25 m/s                                                           0x01

0.75 m/s                                                           0x03

120 m/s                                                          0xc4

238 m/s                                                          0xff
```

#### ADS-L.4.SRD860.G.1.9 Vertical Rate

The resolution is 0.125 m/s. Exponential signed encoding is used with 1 bit for the sign, 2 bits for the exponent, and 6 bits for the base. Positive values shall indicate an upward motion (away from Earth). The minimum and maximum encodable values are -119 m/s and +119 m/s, respectively. Examples:

```
Vertical Rate                                                    Encoded Field (9 bits)

0                                                                0x000

0.125 m/s                                                        0x001

-0.125 m/s                                                       0x101

10 m/s                                                           0x048

-10 m/s                                                          0x148

119 m/s                                                          0x0ff

-119 m/s                                                         0x1ff
```

#### ADS-L.4.SRD860.G.1.10 Ground Track

Track (direction of motion) over ground, in degrees, clockwise orientation. The resolution is 360°/512, i.e. 0.703125°.

#### ADS-L.4.SRD860.G.1.11 Source Integrity Level

```
Source Integrity Level, Probability of Exceeding                   Value
```

Rc

```
Undefined or > 1E-3 per flight hour                                0

<= 1E-3 per flight hour                                            1

<= 1E-5 per flight hour                                            2

<= 1E-7 per flight hour                                            3
```

Please refer to EUROCAE ED-102B / RTCA DO-260B: Minimum Operational Performance Standards (MOPS) for 1090 MHz Extended Squitter Automatic Dependent Surveillance – Broadcast (ADS-B) and Traffic Information Services – Broadcast (TIS-B) for details.

#### ADS-L.4.SRD860.G.1.12 Design Assurance

```
Design Assurance (DAL)                                        Value

Undefined / none                                              0

D                                                             1

C                                                             2

B                                                             3
```

Please refer to EUROCAE ED-102B / RTCA DO-260B for details.

#### ADS-L.4.SRD860.G.1.13 Navigation Integrity

```
Navigation Integrity (Containment Radius Rc)              Value

Undefined                                                 0

>= 20 Nm                                                  1

< 20 Nm                                                   2

< 8 Nm                                                    3

< 4 Nm                                                    4

< 2 Nm                                                    5

< 1 Nm                                                    6

< 0.6 Nm                                                  7

< 0.2 Nm                                                  8

< 0.1 Nm                                                  9

< 75 m                                                    10

< 25 m                                                    11

< 7.5 m                                                   12
```

Please refer to EUROCAE ED-102B / RTCA DO-260B for details.

#### ADS-L.4.SRD860.G.1.14 Horizontal Position Accuracy

```
95% Horizontal Accuracy Bound                             Value

Unknown / no fix or >= 0.5 NM                             0

< 0.5 NM                                                  1

< 0.3 NM                                                  2

< 0.1 NM                                                  3

< 0.05 NM                                                 4

< 30 m                                                    5

< 10 m                                                    6

< 3m                                                      7
```

#### ADS-L.4.SRD860.G.1.15 Vertical Position Accuracy

```
95% Geometric Altitude Accuracy Bound                     Value

Unknown / no fix or >= 150 m                              0

< 150 m                                                   1

< 45 m                                                    2

< 15 m                                                    3
```

#### ADS-L.4.SRD860.G.1.16 Velocity Accuracy

```
95% Horizontal Velocity Accuracy Bound                    Value

Unknown / no fix or >= 10 m/s                             0

< 10 m/s                                                  1

< 3 m/s                                                   2

< 1 m/s                                                   3
```

Please refer to EUROCAE ED-102B / RTCA DO-260B (NACv parameter) for details.

## SUBPART H - CREDITS

This document was initially developed in 2022 by a group of industry stakeholders (the “working group”). Oversight and project management was provided by members of EASA, Credits are due to the following entities and persons:

```
•       Urban Mäder, FLARM Technology
•       Paweł Jałocha, Open Glider Network
•       Jürgen Eckert, Skytraxx

                                                                                             INFORMATION
```

## APPENDIX – PERFORMANCE INFORMATION

Typical airborne transmitter

Transmitter power

```
    -   M-Band – ERP ≤14dBm (≤25mW)
    -   O-Band – ERP ≤27dBm (≤500mW)
Transmitter antenna
    -   Vertically polarized
    -   Omnidirectional
Antenna placement
    -   Suitable performance of device/system has been verified
```

Typical ground receiver

Antenna

```
    -   Vertically polarized collinear
Antenna placement
    -   Unobstructed view of airspace to be monitored
    -   Interferences are managed (e.g. location with less RF sources, signal filtering)
Receiver
    -   Capable of receiving concurrently signals on defined frequencies
    -   Software Defined Radio recommended for future compatibility
```

Typical performance

Reception range

```
-   Expected range at least 10 km for an optimal system setup
-   Maximum observed range of a similar system was greater than 100 km
```
