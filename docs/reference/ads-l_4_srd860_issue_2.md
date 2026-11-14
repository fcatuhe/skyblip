```
           Technical Specification
                                       for
     ADS-L transmissions
using SRD-860 frequency band
                 (ADS-L 4 SRD-860)
```

ACCEPTABLE METHODS, TECHNIQUES AND PRACTICES FOR CARRYING OUT ADS-L TRANSMISSIONS USING SRD-860 FREQUENCY BAND AS PERMITTED PURSUANT TO AMC1 SERA.6005(c) POINT (a)(3)(i)

```
       Issue 2
1 DECEMBER 20251
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
FEC                       Forward Error Correction
FAA                       US Federal Aviation Administration
GCS                       Ground Control Station (for UAS)
GFSK                      Gaussian frequency shift keying
GNSS                      Global Navigation Satellite System, such as GPS
GVA                       Geometric Vertical Accuracy
HDR                       High Data Rate
HFOM                      Horizontal Figure of Merit, horizontal position accuracy
HPL / HIL                 Horiontal Protection Level / Horizontal Integrity Level
IETF / IRTF               Internet Engineering/Research Task Force
IMZ                       eID mandatory zones
ISO                       International Organization for Standardization
ITU                       International Telecommunication Union
JARUS                     Joint Authorities for Rulemaking on Unmanned Systems
LAANC                     Low Altitude Authorization and Notification Capability
LBT                       Listen before talk
LDR                       Low Data Rate
LUT                       Look-up table
LSB                       Least significant bit
m                         Meter, unit of distance, also vertically
MAC                       Media access control
MOPS                      Minimum Operational Performance Standards
MSL                       Mean Sea Level, as used in EGM96
NACp                      Navigation Accuracy Category for Position
NACv                      Navigation Accuracy Category for Velocity
NMEA                      National marine electronics association
Operator                  An entity or individual under which UAS and pilots operate

OSI                     Open Systems Interconnection
Pilot                   A human individual controlling a manned aircraft
PVT                     Position Velocity Time (fix)
R/C                     Remotely controlled
R-S                     Reed-Solomon
RF                      Radio frequency
RID                     Remote Identification
RPAS                    Remotely Piloted Aircraft Systems, the regulator’s current term for
                        UAS
s                       Second, unit of time
SoC                     System on chip
SORA                    Specific Operations Risk Assessment
SRD                     Short-Range Device, a term from CEPT/ECC Recommendation 70-03
SSL                     Secure Sockets Layer
TCPA                    Time to Closest Point of Approach
TRNG                    True Random Number Generator
UAS                     Unmanned Aircraft System, the UAV and connected ground-station
UAV                     Unmanned Aerial Vehicle, also known as a drone, multicopter, RPAS
                        or R/C model aircraft; may carry manned cargo
UTC                     Coordinated Universal Time
UTM / UTMS              UAS Traffic Management System
V2V                     Vehicle to vehicle communications
V2X                     Vehicle to infrastructure communications
VFOM                    Vertical Figure of Merit, vertical position accuracy
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

On December 16, 2021, EASA further published NPA 2021-14: “Development of acceptable means of compliance and guidance material to support the U-space regulation” (“AMC/GM”). Section 3.3 introduced in more detail the AMC and GM for implementing above SERA.6005 (c) article. AMC1 SERA.6005(c) in point (a)(3)(ii) specifies the ADS-L radio system, using the SRD-860 frequency band, as one of the four means of transmission of electronic conspicuity information. Appendix 1 to AMC1 SERA.6005(c) further specifies ADS-L MESSAGE GENERATION FUNCTION i.e. the minimum set of parameters that should be transmitted and a set of parameters that may be transmitted optionally, termed “ADS-L” thereafter. This document contains the specification of the ADS-L radio system. Issue 1 of this document, as published in December 2022, introduced the “Traffic” payload for compliance with SERA.6005(c). This revised Issue 2 adds functions that are beyond compliance with SERA.6005(c) and aim to improve traffic safety, interoperability and usability in general: The “Status” payload improves general interoperability between ADS-L users. The “Traffic Uplink” and “FIS-B” payloads enable services from a ground-based infrastructure. Finally, the “Remote Identification” payload provision is added for for future use of ADS-L for UAV identification and tracking.

### ADS-L.4.SRD860.A.3 Scope

The technical specification is intended to be a complete and accurate description of ADS-L, including physical layer, timing details, data semantics, byte packaging and other technical aspects required for implementing the protocol. It does not cover other aspects of a practical system for electronic conspicuity, such as: User interface, configuration, error handling, software upgrades, or maintenance. It is open to any interested party for implementation. In some of the definitions, C code snippets are used. These snippets are intended to provide easy-to-adopt examples. The snippets are hardware-agnostic and fully portably. The code follows the ANSI C99 standard and assumes (within the context of the example code) a standard integer width of 32 bits and little endian byte order. More formal descriptions will be added later.

### ADS-L.4.SRD860.A.4 Qualification System

There is no ETSO or MOPS for ADS-L at the time of writing of this specification. Manufacturers shall verify and declare compliance of their ADS-L products with this specification. A template for a Declaration of Conformity (DoC), to be issued by the manufacturer, is supplied in the Appendix. ADS-L devices shall comply with local, applicable regulations. In particular, ADS-L devices shall bear the CE marking, complying and declaring conformity with applicable regulations in the domains of safety, radio equipment, electromagnetic compatibility, and use of hazardous substances.

### ADS-L.4.SRD860.A.5 Documentation

All ADS-L equipment offered on the market should be accompanied by proper documentation, allowing pilots and users to optimally use the equipment. Manufacturers of ADS-L equipment shall thus at least provide the following documentation with their products:

```
•   Declaration of Conformity with the ADS-L standard. Refer to the appendix for a template.
•   Declaration of Conformity with all relevant non-aviation standards for obtaining the CE label
•   Installation manual, containing in particular instructions for the installation such that the radio
    performance of the ADS-L equipment is optimized for range, polarity, and omnidirectionality
•   User manual, explaining at least the relevant configuration parts inherent to ADS-L.
```

## SUBPART B - OVERVIEW

### ADS-L.4.SRD860.B.1 Block Diagram

A typical implementation block diagram is given below. The implementation consists of at least:

```
•   GNSS receiver for 3D localization and timing,
•   Host processor
•   RF frontend, capable of transmitting RF signals.
```

In this setup, the processing of the ADS-L protocol happens in the host processor. Alternative setups use an SoC for RF and protocol processing in one package or implement a standalone ADS-L transmitter.

```
                    Host
GNSS                                      RF
                  Processor
```

### ADS-L.4.SRD860.B.2 Protocol Architecture

ADS-L is a lightweight, stateless, undirected broadcast protocol. It loosely follows the ISO/IEC 7498-1 OSI model using layers 1 through3 and 6 through 7 (see below, red). Layers 4 and 5 (grey) are not used.

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
•   Byte size: A byte consists of 8 bits. The least significant bit has a weight of 0x01, while the most
    significant bit has a weight of 0x80 (128 decimal).
•   Byte order: Little Endian byte order is used throughout. That is, integers (of any size) are
    transmitted on the RF least significant byte first.
•   Bit order: Individual bytes are transmitted most significant bit first.
•   Bit numbering: Bits are counted from least significant bit to most significant bit, where the
    counting starts with zero (0).
```

Example 1: The integer 1328922 (0x14471A) is transmitted as 0x1A, 0x47, 0x14 on the radio (from left to right). The exact bit sequence on the RF is: 00011010, 01000111, 00010100 (again left to right). Example 2: Consider the following payload block, taking up a total size of 3 bytes / 24 bits:

```
Offset                Description                    Width (bits)          Encoding

0                     A                              14                    Unsigned integer

14                    B                              10                    Unsigned integer
```

The structure of the payload, as transmitted on the radio, then is as follows:

```
                Byte 0                                   Byte 1                              Byte 2

   7    6   5    4   3   2   1   0     7   6    5    4     3      2   1   0    7   6   5   4    3     2   1     0
A7 A6 A5 A4 A3 A2 A1 A0               B1 B0 A13 A12 A11 A10 A9 A8             B9 B8 B7 B6 B5 B4 B3 B2
```

Transmission Sequence

Consider the example data {A = 0x12AA, B = 0x355}. Using the above construction rule, the payload is transmitted on the radio as Byte 0 = 0xAA, Byte 1 = 0x52, and Byte 2 = 0x55. The exact bit sequence is: 10101010 01010010 11010101 (left to right).

### ADS-L.4.SRD860.B.4 Transmission Sequence

All data is sent in discrete data units called “radio packets”, the size of which is determined by the payload size. The structure of a packet is indicated below.

```
Preamble         Sync Word           ADS-L Packet               Signature

                                                                                 time
```

Preamble and Sync Word are described in Subpart C. The red block is called “ADS-L Packet” (red) and contains the actual, semantic ADS-L information. Its content is described in Subparts D and following. The signature (blue) is optional; its presence must be indicated in the header (see Subpart D).

### ADS-L.4.SRD860.B.5 Soft Versioning and Protocol Evolution

The ADS-L 4 SRD-860 protocol is designed to evolve over time to adapt to changing requirements and to add new functions. Implementors shall add a provision to update the software on any ADS-L 4 SRD 860 device such that an updated protocol specification can be supported even after manufacturing. It is often not feasible to update equipment quickly, especially in certified equipment. Even if every product is updated quickly, a significant transition period still emerges where different protocol versions are in use and interact. Therefore, the protocol must allow for interoperability of different versions. This is particularly a problem for air-to-air interactions: By the connection-less, one-to-many nature of ADS-L 4 SRD-860, a negotiation of protocol versions - as is used by many internet protocols - between individual clients is not possible. ADS-L 4 SRD-860 thus uses the Soft Versioning mechanism that preserves interoperability between different versions of the protocol, while enabling the evolution of the overall network. Soft Versioning is enabled by two fields of the protocol: 1. The “Protocol Version” field is transmitted with every radio transmission (see ADS-L.4.SRD860.E.1, Network Layer) and designates the protocol version used for encoding the payload. 2. The “Max. Protocol Version” field in the Status payload (see ADS-L.4.SRD860.G.2), with which an ADS-L client shall indicate the maximum protocol version it is capable of processing for both transmitting and receiving. An ADS-L client with air-to-air connectivity shall keep track of the Max. Protocol Version of other clients. For other clients for which no Status payload is received, the Protocol Version transmitted as part of the Traffic payload shall be considered as the Max. Protocol Version of that client. When sending a Traffic Payload, the client shall use a protocol version compatible with all other clients according to their declared Max. Protocol Version if one the following conditions holds: 1. The other client is closer than 1 km horizontal and 800 m vertical separation 2. The Time to Closest Point of Approach (TCPA) to the other client is less than 30 seconds, and the vertical separation is under 800 m. TCPA is computed in the horizontal (2D) plane by dividing the horizontal distance between the two aircraft by their closure rate. If multiple clients satisfy the conditions above, the lowest protocol version shall be used satisfying all these clients. For payloads other than Traffic, the client may opportunistically use a lower protocol version than it is technically capable of to improve interoperability.

### ADS-L.4.SRD860.B.6 Reserved Fields

Fields marked as “Reserved” shall generally be ignored by the receiver. That is, an ADS-L sender may fill these fields with arbitrary values without breaking compliance with this protocol specification.

## SUBPART C - PHYSICAL LAYER

### ADS-L.4.SRD860.C.1 Introduction

Two frequency bands are supported by ADS-L 4 SRD860: M-band3 operating in the 868.0 .. 868.6 MHz spectrum on two distinct frequencies / channels, and O-band operating in the 869.4 .. 869.65 spectrum. These bands differ in allowable duty cycle and power, as well as the expected level of interference. The channels are partly derived from the existing FLARM and PilotAware radio systems to improve interoperability of these systems with ADS-L and enable cost-effective retrofits. The O-band High Data Rate channel was added for providing the necessary bandwidth for uplink payloads.

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

#### ADS-L.4.SRD860.C.2.1 Manchester Encoding

Manchester encoding is applied to the Sync Word and the entire ADS-L packet for better clock synchronization (lower error rate and more reliable transmission), halving the effective bit rate to 50kbps. Manchester encoding is not applied to the Preamble, see below. The encoding is such that a ‘1’ is transmitted as ‘01’ and a ‘0’ as ‘10’.

If the (optional) secure signature is used, Manchester encoding is disabled for transmitting it to enable a shorter on-air time. This means the transmitter shall switch to non-Manchester mode after transmitting the ADS-L packet. See the next section on the ADS-L packet structure.

3 See ETSI EN 300 220-2 for a definition of both bands. The band designations (“M-band”, “O-band”) were derived from the ETSI norm, but have changed in since this specification was started. For the sake of consistency, we keep the designation in line with previous versions of the ADS-L 4 SRD 860 specification.

#### ADS-L.4.SRD860.C.2.3 Preamble

The Preamble is sent by the transmitter to allow eligible receivers to synchronize their clocks. It consists of two parts:

```
1.       The binary sequence 01, transmitted at least 5 times, maximum 12 times. The default number
         of repetitions is 8.
2.       The binary sequence 1001, transmitted twice.
```

Manchester encoding is not applied to the Preamble. The shortest valid preamble thus is (sent from left to right): 01 0101 0101 1001 1001.

#### ADS-L.4.SRD860.C.2.4 Sync Word

The Sync Word is 2 bytes. The Sync Word content is given below as arrays, transmitted left to right. Manchester encoding is to be applied to the Sync Word before sending.

```
Hex                                                 0x72 0x4B

Binary                                              01110010 01001011
```

#### ADS-L.4.SRD860.C.2.5 Frequency Diversity

The two frequencies of the M-band are used: 868.2 and 868.4 MHz. An ADS-L sender is free to select either frequency for transmission, e.g. based on current spectrum usage, with the following exception: For the “Traffic” and “Status” payloads (see Subpart G), the sender shall alternate between these frequencies for every transmission to facilitate reception of these payloads for restricted, single-channel receivers.

### ADS-L.4.SRD860.C.3 O-Band Low Data Rate (LDR)

A digital modulation scheme (2-GFSK) is employed to transmit data. The key parameters are given in the table below. Important Notice: Issue 2 of the specification corrects the modulation characteristics for ADS-L in the O-Band (low data rate transmission). The O-Band specification in Issue 1 should not be used, as it will not be compatible with future standards.

```
Frequency                                             869.525 MHz

Channel Bandwidth                                     250 kHz

Modulation                                            GFSK

Max. Power (ERP)                                      27d Bm / 500 mW

Chip rate                                             38.4 kbps

Frequency Deviation                                   0: -12.5 kHz, 1: +12.5k Hz

Gauss Filter BT                                       1.0

Backoff Interval (see Subpart D)                      10 ms .. 50 ms
```

#### ADS-L.4.SRD860.C.3.2 Preamble

The Preamble is sent by the transmitter to allow eligible receivers to synchronize their clocks. It consists of the binary sequence 10, sent from left to right, repeated 40 times.

#### ADS-L.4.SRD860.C.3.1 Sync Word

The Sync Word is 2 bytes. The Sync Word content is given below as arrays, transmitted left to right.

```
Hex                                                 0xB4 0x2B

Binary                                              1011 0100 0010 1011
```

### ADS-L.4.SRD860.C.4 O-Band High Data Rate (HDR)

A separate modulation mechanism is defined for the O-band employing a higher data rate, thus allowing to transmit larger payloads, e.g. for forwarding and uploading data from the ground infrastructure to airborne vehicles. A modern, efficient modulation scheme (GMSK) is employed to use the available band maximally. The key parameters of the modulation are given in the table below:

```
Frequency                                             869.525 MHz

Channel Bandwidth                                     250 kHz

Modulation                                            GMSK

Max. Power (ERP)                                      27d Bm / 500 mW

Chip rate                                             200 kbps

Frequency Deviation (implicit)                        0: -50 kHz
                                                      1: +50 kHz

Gauss Filter BT                                       0.5

Backoff Interval (see Subpart D)                      10 ms .. 50 ms
```

#### ADS-L.4.SRD860.C.4.2 Preamble

The Preamble is sent by the transmitter to allow eligible receivers to synchronize their clocks. It consists of a binary sequence 10, sent from left to right, repeated 12 times.

#### ADS-L.4.SRD860.C.4.3 Sync Word

The Sync Word is 2 bytes. Its content is given below as arrays, transmitted left to right.

```
Hex                                                  0x2D 0xD4

Binary                                               0010 1101 1101 0100
```

### ADS-L.4.SRD860.C.5 Time Multiplexing

Different ADS-L payloads have differing requirements in terms of reliability, urgency, latency, and bandwidth usage. For instance, a Traffic payload sent by an airborne transmitter is more important and more time-critical than a FIS-B Uplink payload sent by a ground station, but the latter may also contain much more data, thus occupy more bandwidth. To isolate these different payloads and applications and thus preserve quality of service, a time multiplexing scheme is employed for the HDR channel: Spectrum access is restricted to certain time slots, depending on the payloads transmitted. Time slots are expressed relative to the full second of UTC time and the cycle repeats every second. Transmitters are expected to have an accurate time base, e.g. obtained from a GNSS source or a network. Transmissions must be completed before the end of the slot. Three different slots are currently employed:

```
•     Direct: Reserved for payloads of immediate relevance, urgency and with implications to safety,
      transmitted from an airborne ADS-L client. This slot lasts from 450 ms to 1000 ms, relative to the
      full UTC seconds. Currently, these are the following payloads: “Traffic”, “Status”, “Remote
      Identification”
•     Uplink: Reserved for payloads transmitted from the ground infrastructure. This slot lasts from
      200 ms to 450 ms relative to the full second. Currently, these are the following payloads: “Traffic
      Uplink” and “FIS-B Uplink”.
•     Reserved: The unused time is reserved for future extensions and shall not be used for any ADS-L
      transmissions.
```

## SUBPART D - DATA LINK LAYER

### ADS-L.4.SRD860.D.1 Data Link Packet Structure

The data link layer governs the following aspects of the protocol:

```
•   Mediating access to the RF channel for maximum scalability and fairness
•   Ensuring data integrity by means of adding a parity field (CRC or FEC).
•   Optionally, authenticating the message by adding a secure signature
```

For the purpose of supporting legacy radio systems, the O-band LDR channel uses a different data link packet structure as the other channels.

#### ADS-L.4.SRD860.D.1.1 M-band and O-band HDR

The structure of the data link packet (ADS-L packet) is given as:

```
Offset                       Description         Width (bits)       Encoding

0                            Packet Length       8                  Length of the Data field, in
                                                                    bytes if no secure signature is
                                                                    present (see Subpart E).
                                                                    Otherwise, length of the Data
                                                                    field in bytes minus 68.

8                            Data                (Packet            See Subpart E
                                                 Length) * 8
```

The Packet Length field contains the number of bytes of the full message including the header, payload, and parity, but excluding the length field itself, and the secure signature (if present). Omitting the length of the secure signature field facilitates the reception of packets with some simple clients (integrated radio chips), while complex receivers can check the Secure Signature Flag field in the Network header to receive the secure signature as well. Some payload types have variable lengths, in which case the receiver must consult this field to determine the true payload length.

#### ADS-L.4.SRD860.D.1.2 O-band LDR

This channel uses a custom data link layer for compatibility with legacy hardware, replacing Subpart D. The secure signature (see ADS-L.4.SRD860.E.4) and the forward error correction (see ADS-L.4.SRD860.E.3.2) shall not be used with this channel. The maximum payload length is 15 bytes (excluding headers and checksums). Smaller payloads may be transmitted by padding with arbitrary data. The structure of the data link packet (ADS-L packet) for O-band LDR is given as:

```
Offset                      Description        Width (bits)       Encoding

0                           Header             48                 Fixed: 0x00, 0x00, 0x00, 0x00,
                                                                  0x18, 0x71

48                          Data               192                See Subpart E

240                         CRC8               8                  Checksum
```

The CRC8 checksum is in addition to the error control in ADS-L.4.SRD860.E.3 for interoperability with legacy systems. It is computed as follows:

static uint8_t _crc8_update(uint8_t input, uint8_t crc) { const uint8_t poly = 0x07; crc ^= input; for (uint8_t bit = 0; bit < 8; bit++) { if (crc & 0x80) { crc = (crc << 1) ^ poly; } else { crc = (crc << 1); } } return crc; }

uint8_t crc8(uint8_t *data, int len_data) { uint8_t crc = 0x71; // initial value for (size_t idx = 0; idx < len_data; idx++) crc = _crc8_update(data[idx], crc); return crc; }

### ADS-L.4.SRD860.D.2 Sequence of Construction

The sequence of construction of the ADS-L packet is shown in the following diagram, from the point of view of a sender:

```
                            ADS-L Data
                             Subpart F

                             Scramble
                            Section E.2

Network Header
   Subpart E

                           Error Control
                            Section E.3

Datalink Header
  Subpart D

                                                       Secure Sig
                           ADS-L Packet
                                                      Section D.1.3
```

Description: 1. The ADS-L Data content is first formed according to Subpart F 2. Scrambling, if enabled, is applied per Section ADS-L.4.SRD860.E.2 3. Error control / parity according to Section ADS-L.4.SRD860.E.3 is appended 4. The network header is formed according to Subpart E and prepended to the (previously scrambled) content 5. Optionally, a secure signature is appended. The signature is built by completely digesting and signing the previously formed ADS-L packet, i.e. including headers and parity

### ADS-L.4.SRD860.D.3 Media Access

The MAC protocol to be used is Carrier-Sense Multiple Access (CSMA). Listen-before-talk (LBT) must be implemented according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1.

If no packet can be transmitted 3000 ms after the initial attempt, the device may force transmission irrespective of carrier detect. After a forced transmission, the device may not transmit for at least 2000 ms.

## SUBPART E - NETWORK LAYER

### ADS-L.4.SRD860.E.1 Network Packet Structure

The network layer governs the following aspects of the ADS-L protocol: • Protocol versioning, such that newer versions of the protocol can be safely deployed • The optional secure signature for sender authentication • Data scrambling, improving the resilience to transmission errors and improving the radio channel characteristics.

The network packet structure is given as:

```
Offset                       Description          Width (bits)        Encoding

0                            Protocol             4
                             Version

4                            Secure               1                   Indicates presence of the
                             Signature flag                           secure signature:
                                                                      0: No signature present
                                                                      1: Signature present

5                            Key Index            2                   Designates the key used, 0 for
                                                                      public

7                            Error Control        1                   Indicates the mode of error
                             Mode / Parity                            detection / correction
                             type                                     algorithm:
                                                                      0: CRC
                                                                      1: FEC

8                            ADS-L Data           Variable            See Subpart F

Variable                     Parity               24 or 256           Depends on Error Control
                                                                      Mode, see Section E.3

Variable                     Secure               544                 Optional
                             Signature
```

The fields Protocol Version, Secure Signature Flag, Key Index, and Error Control Mode are called the Network Header. The individual fields are further explained below.

#### ADS-L.4.SRD860.E.1.1 Protocol Version

This field shall be set to the version of the ADS-L 4 SRD-860 protocol specification being used for encoding the Presentation and Application layers of the packet. If multiple versions of the specification use identical encoding for the payload that is being sent as part of this ADS-L packet, then the lowest version shall be transmitted for maximum interoperability.

```
ADS-L 4 SRD 860 Specification                     Value

Issue 1                                           0

Issue 2                                           1
```

Also see ADS-L.4.SRD860.B.4 for a description of the Soft Versioning mechanism.

#### ADS-L.4.SRD860.E.1.2 Secure Signature Flag

If set to 1, the Secure Signature field shall be included, otherwise omitted.

#### ADS-L.4.SRD860.E.1.3 Key Index

This field indicates which key is used for the encryption / scrambling of the header and payload. By default, key 0 is used for data scrambling. Low-power or constrained ADS-L clients may optionally use key index 3 to completely disable data encryption / scrambling.

```
Key Index                                         Key

0                                                 0x00000000

1                                                 Reserved

2                                                 Reserved

3                                                 Encryption / scrambling disabled
```

### ADS-L.4.SRD860.E.2 Encryption / Data Scrambling

The ADS-L Data block is optionally scrambled for improved error detection. Data encryption for enhanced privacy is reserved for future version of ADS-L 4 SRD 860, but described here for completeness. The XXTEA symmetric encryption algorithm with 6 rounds shall be used. The algorithmic network of the cipher is given below. An example C implementation is also given below. The required parameters are indicated in the table below.

The part of the packet to be scrambled/encrypted with XXTEA shall be a multiple of four bytes as XXTEA works on 32-bit words. For data scrambling, the XXTEA algorithm is used with a zero encryption key. Scrambling enables an additional verification check as the scrambled packet content is sensitive to single bit errors which become immediately noticeable and they make the packet look pseudo-random. The following sample C function implements the above XXTEA network. The code is applicable only to systems with a little-endian byte order. The data argument points to the “ADS-L Data” block, which contains the clear-text data, serialized according to Subparts F and G. The data is re-interpreted as an array of 4-byte integers, using (again) little-endian byte order. The num_data_words argument contains the size of the “ADS-L Data” block, in words, i.e. 1/4 of the size in bytes. The function scrambles the block in-place, i.e. writes its result back to data.

```
#define MX       ((((z>>5)^(y<<2))+((y>>3)^(z<<4)))^((sum^y)+(key[(p&3)^e]^z)) )
```

void xxtea_encrypt(uint32_t* data,

```
uint32_t num_data_words,
const uint32_t* key,
uint32_t round)
```

{ uint32_t z, y = data[0], sum = 0, e, DELTA = 0x9e3779b9; uint32_t p, q; z = data[num_data_words - 1]; q = round; while (q-- > 0) { sum += DELTA; e = sum >> 2 & 3; for (p = 0; p < num_data_words - 1; p++) { y = data[p + 1]; data[p] += MX; z = data[p]; }

```
y = data[0];
data[num_data_words - 1] += MX;
z = data[num_data_words - 1];
```

} }

```
Encryption Parameter                                               Value

Round                                                              6

num_data_words                                                     6
```

### ADS-L.4.SRD860.E.3 Error Control

Two distinct methods are applied for detecting and correcting errors: A cyclic redundancy check (CRC) is used for smaller payload sizes that are transmitted frequently, e.g. traffic data. For larger, static payloads, a forward error correction (FEC) is recommended that allows to detect and correct errors, e.g. due to radio interference. Error control is applied to the entire Network Packet Structure, i.e. including the fields Protocol Version, Secure Signature flag, Key Index, and Error Control Mode. The mode of error control is determined by the “Error Control Mode” flag in the network layer header. A “0” indicates CRC, a “1” FEC. The two modes are distinct in how and how much parity is computed and added to the data; refer to the sections below for details. Note: Issue 1 of this specification only defined the CRC variant. The description of CRC was moved from Subpart D to E, but the description in this revision is compatible with Issue 1.

#### ADS-L.4.SRD860.E.3.1 CRC

In this mode, a 24-bit cyclic redundancy check (CRC) is added to the packet, ensuring message integrity. The CRC is to be computed over the entire Network Packet Structure (Subpart E), i.e. excluding the Data Link fields.

The CRC code is identical to the one used in Mode-S and 1090 MHz ADS-B. The generator polynomial is thus given by:

```
G(x) = x24 + x23 + x22 + x21 + x20 + x19 + x18 + x17 +
      x16 + x15 + x14 + x13 + x12 + x10 + x3 + 1
```

A sample implementation is given below: static uint32_t _crc24_polypass(uint32_t crc, uint8_t input) { const uint32_t poly = 0xFFFA0480; crc |= input; for (uint8_t bit = 0; bit < 8; bit++) { if (crc & 0x80000000) crc ^= poly; crc <<= 1;

} return crc; }

uint32_t crc24(const uint8_t *data, size_t len_data) { uint32_t crc = 0; for (size_t idx = 0; idx < len_data; idx++) { crc = _crc24_polypass(crc, data[idx]); } crc = _crc24_polypass(crc, 0); crc = _crc24_polypass(crc, 0); crc = _crc24_polypass(crc, 0); return crc >> 8; }

Note that only the 3 lower bytes of the result are relevant and to be transmitted on the RF in Big endian, i.e. most significant byte first. This byte order allows certain discrete radio chips to use hardware-accelerated CRC generation and verification, reducing power consumption for these platforms.

#### ADS-L.4.SRD860.E.3.2 FEC

In this mode, a Reed-Solomon (R-S) Forward Error Correcting code is used to not only verify integrity of the transmitted message, but to also increase the capability to correct for transmission errors. This should be used for ADS-L messages that are longer than 64 bytes overall, though operators may deviate from this recommendation based on the current channel conditions. The parameters for R-S are chosen as follows:

```
Parameter                               Value

Symbol size                             8 bits (1 Byte)

Number of parity symbols                32 Bytes

Generator polynomial                    1+x2+x3+x4+x8

Primitive element                       11

First consecutive root                  121
```

R-S FEC is computed over the network header and the ADS-L data in encrypted / scrambled form, as defined in Section. ADS-L.4.SRD860.E.1. R-S expects a data block of exactly (255-32) = 223 Bytes; if the data to be transmitted is smaller, it is to be prefixed (padded) with zeros for the computation of the parity. The maximum size of ADS-L data supported by FEC is thus 223 bytes. This is depicted in the figure below from the point of view of the sender: The Network Header and the ADS-L data, as described in Subpart F and after encryption is applied, are combined, then considered as one data block with a size depending on the size of the ADS-L data. The block is prefixed with zeros (padding) to create a data block of 223 bytes. The R-S parity is then computed over the following fields:

```
•   The padding block
•   The network header, as defined in ADS-L.4.SRD860.E.1
•   The ADS-L data in encrypted / scrambled form, as defined in Section ADS-L.4.SRD860.E.1
```

The resulting parity block is added to the packet. Only the Network Header, ADS-L Data and Parity blocks are transmitted. The Padding is implicit and not transmitted.

```
                       Network
                                        ADS-L Data (Subpart F, scrambled)
                       Header

                       Network
Padding (0x00)                          ADS-L Data (Subpart F, scrambled)
                       Header

                       Network
                                        ADS-L Data (Subpart F, scrambled)               Parity
                       Header
```

### ADS-L.4.SRD860.E.4 Secure Signature

ADS-L clients may optionally digitally sign the transmitted data packets using a secure public-key digital signature algorithm to improve the trustworthiness of their transmission. The private key shall be stored securely on the device and be protected against all reading. Only the associated public key may ever be exposed. A device may implement automatic creation of a new public key, e.g. by means of a true random number generator. The digital signature links a physical device, represented by its secret private key, to a real entity (legal or natural). This relationship is typically stored in a certificate authority (CA), operated e.g. by the competent authority. CA and other infrastructure are outside the scope of this document. The secure signature is optionally appended to the payloads after the Parity field. It digests the complete ADS-L packet, as it is transmitted. For details, see Section ADS-L.4.SRD860.D.1.1. The size of the secure signature is not reflected in the Packet Length field; simple receivers may thus receive messages while ignoring the signature. For M-band, Manchester encoding is suspended for the transmission of the signature to achieve a higher data rate and minimize spectrum usage. The secure signature shall be used at most every 10 seconds to conserve spectrum. The structure of the secure signature block is as follows:

```
Offset                        Description          Width (bits)           Encoding

0                             Timestamp            32                     Seconds

32                            Signature            512                    R, S components of the
                                                                          Ed25519 signature
```

#### ADS-L.4.SRD860.E.4.1 Timestamp

The Timestamp field contains the global time at the time of transmission of the packet, encoded in seconds since January 1st, 1970 (UNIX Epoch) in UTC. Unsigned encoding is used.

#### ADS-L.4.SRD860.E.4.2 Signature

The algorithm is the Edwards-curve Digital Signature Algorithm (EdDSA), specifically the Ed25519 variant as described in IRTF/IETF RFC 8032. The public key is 256 bits wide, while the signature is 512 bits in compressed form, comprising the two components R and S. The signature field simply contains the R and S components. The SHA-512 digest for the signature is computed over the following fields:

```
•   The network header, as defined in ADS-L.4.SRD860.E.1
•   The ADS-L data in encrypted / scrambled form, as defined in Section ADS-L.4.SRD860.E.1
•   The Timestamp field, as defined in Section ADS-L.4.SRD860.E.4
```

The parity field is not digested.

## SUBPART F - PRESENTATION LAYER

### ADS-L.4.SRD860.F.1 ADS-L Data

The Presentation Layer builds on top of the Network Layer, adding the concrete ADS-L functions. It identifies the sender and provides a means to enumerate the concrete payload type. The data layout of the ADS-L data is given as:

```
Offset              Description          Width (bits)               Encoding

0                   ADS-L Header         40                         See below

40                  ADS-L Payload        Variable, must be 24       Variable, see Subpart G
                                         + (N*32) where N is
                                         a nonnegative
                                         integer
```

### ADS-L.4.SRD860.F.2 ADS-L Header

```
Offset              Description          Width (bits)              Encoding

0                   Payload Type         8                         Payload type discriminator, see
                    Identifier                                     below

8                   Sender Address       30                        Unique address of the sender,
                                                                   see below

38                  Reserved             1

39                  Relay/forward        1                         This packet has been
                                                                   retransmitted/relayed/forwarded
                                                                   on behalf of the sender above
```

#### ADS-L.4.SRD860.F.2.1 Payload Type Identifier

This field defines the structure of the payload field. The upper half of the available range is reserved for unicast payloads, implying a specific structure of the payload, see below.

```
Value           Transmission Type            Payload

0               Broadcast                    Reserved

1               Broadcast                    Reserved

2               Broadcast                    Traffic

3               Broadcast                    Status

   4              Broadcast                       Traffic Uplink

   5              Broadcast                       FIS-B Uplink

   6              Broadcast                       Remote Identification

   7              Broadcast                       Reserved

   …              Broadcast                       …

   65             Broadcast                       Reserved

   66             Broadcast                       OGN Diagnostics4

   67             Broadcast                       Reserved

   …              Broadcast                       …

   126            Broadcast                       Reserved

   127            Broadcast                       Reserved

   128            Unicast                         Reserved

   129            Unicast                         Reserved

   ..             Unicast                         …

   255            Unicast                         Reserved
```

#### ADS-L.4.SRD860.F.2.2 Sender Address

```
   Offset       Description                               Width (bits)

   0            Address Mapping Table                     6

   6            Address                                   24

   Address Mapping Table (AMT)                                           Value

   Random / Privacy                                                      0

   Reserved                                                              1..3

   Reserved                                                              4

   ICAO                                                                  5

   FLARM (including OEMs)                                                6

Address Mapping Table (AMT)                                       Value

OGN-Tracker                                                       7

FANET (including OEMs)                                            8

Manufacturers Page 0                                              9

Manufacturers Page 1                                              10

…                                                                 …

Manufacturers Page 54                                             63
```

Entries 9 and higher of the address mapping table imply a further structuring of the address:

```
Offset       Description                          Width (bits)

0            Manufacturer prefix                  8

8            Base address                         16
```

Manufacturers intending to implement ADS-L shall apply for a unique manufacturer prefix, associated to one of the manufacturer pages of the AMT. The combination AMT page + manufacturer prefix shall be uniquely assigned to only one manufacturer. The manufacturer shall use the remaining 16-bit base address space as densely as possible. Once the range is used up, the manufacturer shall apply for a new AMT page + manufacturer prefix combination to use. To obtain an AMT entry, contact registry@ads-l.aero.

4 Payload definition is outside the scope of this document.

#### ADS-L.4.SRD860.F.2.3 ICAO Address

For this setting, the ICAO address shall be configurable and entered by the user of the ADS-L system, thereby overriding the address given by the manufacturer. This is the preferred mode for aircraft that have an ICAO address (Mode-S transponder code). This setting shall be revertible by user configuration. For aircraft that concurrently transmit using other e-conspicuity systems such as ADS-B, or aircraft that use a Mode-S transponder, it shall use this type of address to allow for disambiguation and attribution of the different radio transmissions on the receiver side. The ADS-L system shall enforce this by disallowing inconsistent user configurations.

#### ADS-L.4.SRD860.F.2.4 Privacy Mode

In order to not disclose its identity, a device may be operated in privacy mode. In this mode, the address is selected randomly. Privacy mode is activated by selecting entry 0 of the AMT. The sender shall randomly and uniformly select manufacturer prefix and base address at device start-up. This random address shall not change while the device is operated. Toggling between privacy and normal modes during flight is not admissible.

#### ADS-L.4.SRD860.F.2.5 ADS-L Payload

The payload structure and semantics are defined by the Payload Type Identifier. Individual payloads are defined in Subpart G. Any new payload (i.e. not defined herein) must be approved by the governing body before using it. Any new payload shall be of a size appropriate for encryption, i.e. (size of the ADS-L Header) + (size of the ADS-L payload) must be a multiple of 4 bytes. Valid payload sizes (in bytes) thus are: 3, 7, 11, 15, etc.

## SUBPART G - APPLICATION LAYER

### ADS-L.4.SRD860.G.1 Traffic Payload

The Traffic payload is designed to comply with AMC1 SERA.6005(c) as proposed in EASA AMC and GM to SERA — Issue 1, Amendment 6. For easier readability, related data are grouped together in line with the “Data Type” column in the NPA. The bit offset in the following tables always refer to the start of the payload. The structure of the payload is given by:

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
                                                                  0.. 238 m/s, 0.25m/s step at
                                                                  lower range

72                  Altitude above WGS-           14              exp. encoding,
                    84 ellipsoid                                  -320 m .. +61112m
                                                                  1m step at lower range

86                  Vertical rate                 9               exp. encoding
                                                                  +/-119 m/s, 0.125m/s step at
                                                                  lower range

95                  Ground track                  9               cyclic: 1bit = 360/512 ≈ 0.7deg

104                 Source integrity level        2               Table

106                 Design assurance              2               Table

108                 Navigation integrity          4               Table

112                 Horizontal position           3               Table
                    accuracy

115                 Vertical position             2               Table
                    accuracy

117                 Velocity accuracy             2               Table

119                 Reserved                      1
```

The individual fields are further explained in the following Sections. Where no valid information of a field is available, the ADS-L transmitter shall transmit "invalid" values for the respective field.

#### ADS-L.4.SRD860.G.1.1 Timestamp

The Timestamp field indicates the number of quarter seconds since the full hour, modulo 60. The values 60..63 shall not be used. The timestamp refers to the time of the navigation solution (PVT fix) obtained by the GNSS receiver (e.g. on the full second) and included in the Traffic payload. It shall be accurate to ± 10 ms. This is distinct from the time of transmission: Calculation of the navigation solution, processing on the host controller, and spectrum access management all introduce small delays. Also see Section ADS-L.4.SRD860.G.1.16.

#### ADS-L.4.SRD860.G.1.2 Flight State

```
Flight State                                                      Value

Undefined                                                         0

On ground                                                         1

Airborne                                                          2

Reserved                                                          3
```

The “Undefined” value shall be used if no reasonable decision is possible by the ADS-L device. The following methods (or a combination thereof) may be used to determine the flight state: 1. Geometric conditions based on the measurements from the GNSS receiver. Conditions may, for instance, include the ground speed passing a threshold or the position deviating from an initial position by a certain distance. 2. Additional sensory information available (directly, or through a connected system) to the ADS-L device that explicitly measures the flight state, such as a weight-on-wheels sensor, barometric sensor, or dynamic pressure (IAS) sensor.

#### ADS-L.4.SRD860.G.1.3 Aircraft Category

```
Aircraft Category                                                     Value

No emitter category information available                             0

Light fixed wing (< 7031 kg / 15 500 lbs)                             1

Small to heavy fixed wing (≥ 7031 kg / 15 500 lbs)                    2

Light Rotorcraft                                                      3

Glider / sailplane                                                    4

Aircraft Category                                                 Value

Lighter-than-air                                                  5

Ultralight and motorized hang-glider                              6

Paraglider                                                        7

Parachutist / skydiver / wingsuit                                 8

eVTOL / UAM                                                       9

Gyrocopter                                                        10

UAS Open category                                                 11

UAS Specific category                                             12

UAS Certified category                                            13

Model plane                                                       14

Heavy Rotorcraft                                                  15

Hang-glider                                                       16

Paramotor                                                         17

Reserved                                                          18

…                                                                 …

Reserved                                                          31
```

#### ADS-L.4.SRD860.G.1.4 Emergency Status

```
Emergency Status                                              Value

Undefined                                                     0

No emergency                                                  1

General emergency                                             2

Lifeguard/medical emergency                                   3

No communications                                             4

Unlawful interference                                         5

Downed aircraft                                               6

Reserved                                                      7
```

#### ADS-L.4.SRD860.G.1.5 Latitude and Longitude

The WGS-84 reference system is used throughout. The position expressed in degrees latitude and longitude, respectively. Both components are encoded as 24-bit signed integers. The least significant bit for the latitude is 1° / 93206; for longitude 1° / 46603. North and East are positive, respectively. If no valid (2D) fix is established by the GNSS receiver, a value of 0x800000 shall be used to indicate this.

#### ADS-L.4.SRD860.G.1.6 Exponential Encoding

The following fields use exponential encoding with varying bit sizes. The two leading bits of the encoded value are used as a scaling exponent, allowing for a much larger value range to be encoded, while keeping high accuracy around zero. Exponential encoding is used for unsigned (altitude, ground speed) and signed (vertical rate) fields. For signed fields, the exponent is shifted one bit to the right, using the (now free) leading bit as a sign bit: If the bit is set, the value encoded by base and exponent is to be interpreted as negative. The layout of an encoded unsigned value thus is:

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

For signed encoded values, the sign field shall indicate that the decoded value is negative, i.e. the above result multiplied by -1. For encoding unsigned values, the following algorithm shall be used: 1. Find the largest e* from the set (0, 1, 2, 3) such that

```
value >= 2N+e* – 2N
```

2. Return

```
exponent = e*
base = (value - 2N+e* + 2N ) / 2e*
```

For encoding signed values, the sign bit shall be set if value is negative. Exponent and base shall be computed as above using abs(value) as input. A value of 0 shall always be encoded as non-negative, i.e. the sign bit shall be cleared. In addition to the exponential encoding, the fields use a scaling factor and – the altitude – an offset. These parameters are indicated below.

#### ADS-L.4.SRD860.G.1.7 Altitude above WGS-84 Ellipsoid

The resolution of the encoding is 1 m. An offset of 320 m is added to encode negative altitudes using only unsigned encoding. Exponential (unsigned) encoding is used with 2 bits for the exponent and 12 bits for the base. The minimum encodable altitude is -320 m, the maximum is 61112 m. If the measured altitude exceeds these limits, the limit values shall be encoded. If no 3D fix and hence no altitude is available, the field shall be marked as invalid.

Examples:

```
Altitude                                                                Encoded Field (14 bits)

-320 m or less                                                          0x0000

0m                                                                      0x0140

1000 m                                                                  0x0528

61104 m or more                                                         0x3ffe

Invalid                                                                 0x3fff
```

#### ADS-L.4.SRD860.G.1.8 Ground Speed

The resolution for encoding ground speed is 0.25 m/s. Exponential (unsigned) encoding is used with 2 bits for the exponent and 6 bits for the base. The maximum encodable ground speed is 236 m/s. If the measured ground speed exceeds this limit, the maximum encodable value shall be encoded. If no ground speed is available, this field shall be marked as invalid. Examples:

```
Ground Speed                                                          Encoded Field (8 bits)

0 m/s                                                                 0x00

0.25 m/s                                                              0x01

0.75 m/s                                                              0x03

120 m/s                                                               0xc4

236 m/s or more                                                       0xfe

Invalid                                                               0xff
```

#### ADS-L.4.SRD860.G.1.9 Vertical Rate

The resolution is 0.125 m/s. Exponential signed encoding is used with 1 bit for the sign, 2 bits for the exponent, and 6 bits for the base. Positive values shall indicate an upward motion (away from Earth). The minimum and maximum encodable values are -118 m/s and +119 m/s, respectively. If the measured vertical rate exceeds these limits, the limit values shall be encoded. If no vertical rate is available, this field shall be marked as invalid. Examples:

```
Vertical Rate                                                         Encoded Field (9 bits)

0                                                                     0x000

0.125 m/s                                                             0x001

-0.125 m/s                                                            0x101

10 m/s                                                                0x048

-10 m/s                                                        0x148

119 m/s or more                                                0x0ff

-118 m/s or less                                               0x1fe

Invalid                                                        0x1ff
```

#### ADS-L.4.SRD860.G.1.10 Ground Track

Track (direction of motion) over ground, in degrees, clockwise orientation, relative to true north. The resolution is 360°/512, i.e. 0.703125°. The Ground Track field shall be considered invalid if the Ground Speed field is zero or marked invalid.

#### ADS-L.4.SRD860.G.1.11 Source Integrity Level

Integrity level of the navigation source. If the navigation source does not hold an applicable ETSO, the value “Undefined” shall be encoded.

```
Source Integrity Level (SIL), Probability of                     Value
```

Exceeding Rc

```
Undefined or SIL > 1E-3 per flight hour                          0

1E-5 per flight hour < SIL ≤ 1E-3 per flight hour                1

1E-7 per flight hour < SIL ≤ 1E-5 per flight hour                2

SIL ≤ 1E-7 per flight hour                                       3
```

Refer to EUROCAE ED-102B / RTCA DO-260C: Minimum Operational Performance Standards (MOPS) for 1090 MHz Extended Squitter Automatic Dependent Surveillance – Broadcast (ADS-B) and Traffic Information Services – Broadcast (TIS-B) for details.

#### ADS-L.4.SRD860.G.1.12 Design Assurance

```
Design Assurance (DAL)                                           Value

Undefined / none                                                 0

D                                                                1

C                                                                2

B                                                                3
```

Refer to EUROCAE ED-102B / RTCA DO-260C for details.

#### ADS-L.4.SRD860.G.1.13 Navigation Integrity

```
Navigation Integrity (Containment Radius Rc)                    Value

Undefined                                                       0

Rc ≥ 20 NM                                                      1

8 NM ≤ Rc < 20 NM                                               2

4 NM ≤ Rc < 8 NM                                                3

2 NM ≤ Rc < 4 NM                                                4

1 NM ≤ Rc < 2 NM                                                5

0.6 NM ≤ Rc < 1 NM                                              6

0.2 NM ≤ Rc < 0.6 NM                                            7

0.1 NM ≤ Rc < 0.2 NM                                            8

75 m ≤ Rc < 0.1 NM                                              9

25 m ≤ Rc < 75 m                                                10

7.5 m ≤ Rc < 25 m                                               11

Rc < 7.5 m                                                      12

Reserved                                                        13

Reserved                                                        14

Reserved                                                        15
```

Refer to EUROCAE ED-102B / RTCA DO-260C for the definition of the Navigation Integrity Category. Encoding may differ from these documents.

#### ADS-L.4.SRD860.G.1.14 Horizontal Position Accuracy

The horizontal position accuracy or Horizontal Figure of Merit (HFOM) is defined as the radius of a circle in the horizontal plane, centered on the reported position, such that the probability of the actual horizontal position lying outside the circle is 0.05. The HFOM is encoded as a discrete category, equivalent to the Navigation Accuracy Category for Position (NACp) field of EUROCAE ED-102B / RTCA DO-260C.

```
95% Horizontal Accuracy Bound (HFOM)                            Value

Unknown / no fix or HFOM ≥ 0.5 NM                               0

0.3 NM ≤ HFOM < 0.5 NM                                          1

0.1 NM ≤ HFOM < 0.3 NM                                          2

0.05 NM ≤ HFOM < 0.1 NM                                         3

30 m ≤ HFOM < 0.05 NM                                           4

10 m ≤ HFOM < 30 m                                              5

3 m ≤ HFOM < 10 m                                               6

HFOM < 3m                                                       7
```

When Horizontal Position Accuracy (or Horizontal Figure of Merit, HFOM) is not available directly (e.g. in systems with legacy GNSS receivers), it shall be derived from Horizontal Dilution of Precision (HDOP) according to the following formula, according to Section A1.2.5.8 of ETSO-C199 A1:

```
HFOM = 2 * HDOP * User Equivalent Range Error (UERE)
```

where the UERE is 6 metres.

#### ADS-L.4.SRD860.G.1.15 Vertical Position Accuracy

The vertical position accuracy or Vertical Figure of Merit (VFOM) is defined as the interval of the size 2*VFOM, centered on the reported altitude, such that the probability of the actual altitude lying outside the interval is 0.05. The VFOM is encoded as a discrete category, equivalent to the Geometric Vertical Accuracy (GVA) field of EUROCAE ED-102B / RTCA DO-260C.

```
95% Geometric Altitude Accuracy Bound                           Value
(VFOM)

Unknown / no fix or VFOM ≥ 150 m                                0

45 m ≤ VFOM < 150 m                                             1

10 m ≤ VFOM < 45 m                                              2

VFOM < 10 m                                                     3
```

#### ADS-L.4.SRD860.G.1.16 Velocity Accuracy

The velocity accuracy is based on 95% bounds on the errors in the measured horizontal velocity. The velocity accuracy is encoded as a discrete category, equivalent to the Geometric Navigation Accuracy Category for Velocity (NACv) field of EUROCAE ED-102B / RTCA DO-260C.

```
95% Horizontal Velocity Accuracy Bound                          Value
(AccVel)

Unknown / no fix or AccVel ≥ 10 m/s                             0

3 m/s ≤ AccVel < 10 m/s                                         1

1 m/s ≤ AccVel < 3 m/s                                          2

AccVel < 1 m/s                                                     3
```

#### ADS-L.4.SRD860.G.1.16 Transmit Rate and Timing

The Traffic payload should be transmitted at a rate of at least 1 Hz whenever a valid and up-to-date PVT fix is available from the GNSS receiver. If the flight state is “On ground”, the transmit rate shall be reduced to 0.1 Hz (once every 10 seconds). The rules regarding media access (see Section ADS-L.4.SRD860.D.4) have higher priority than the above. A message shall be replaced with a new message if the total delay of 500 ms from obtaining (measuring) the PVT fix is exceeded, e.g. due to the frequency being occupied, to prevent old data being sent. No Traffic message shall be sent containing navigation data older than 500 ms.

#### ADS-L.4.SRD860.G.1.17 Quality Indicators

The Quality indicators SIL, SDA, NIC, Horizontal Position Accuracy (HFOM), Vertical Position Accuracy (VFOM), and Velocity Accuracy all define the likelihood of erroneous data being transmitted to ground stations and to other aircraft. ADS-L devices are initially intended to be non-certified and are therefore not permitted to claim certified integrity or assurance. Consequently, the Source Integrity Level (SIL) and System Design Assurance (SDA) parameters shall be set statically to 0. Unlike the ADS-B certification approach (CS-STAN Configuration 3 in particular), uncertified ADS-L devices shall however not report all quality values as 0. Instead, they shall report their positioning performance dynamically as defined in the previous sections. The Navigation Integrity Category (NIC) shall only be set dynamically if the GNSS source provides a valid horizontal protection bound (HPL / HIL); otherwise, it shall be set to 0. This approach ensures that ADS-L messages remain clearly identified as for awareness only, while still conveying useful accuracy information. It also leaves room for future growth and using other values for quality indicators if approval processes are developed for devices with demonstrable assurance levels, while remaining compatible with ADS-B.

### ADS-L.4.SRD860.G.2 Status Payload

The Status payload is used to transmit static information on the sender, describing its capabilities and current state with respect to ADS-L and other, related, systems. The structure of the payload is given by:

```
Offset              Description                  Width (bits)       Encoding

0                   Max. Protocol Version        4                  None

4                   ADS-L M-Band Receive         2                  Table

6                   ADS-L O-Band LDR             2                  Table
                    Receive

8                   ADS-L O-Band HDR             2                  Table
                    Receive

10                  ADS-L 4 Mobile               2                  Table

12                  E-Conspicuity Receive        8                  Bitmap

20                  E-Conspicuity Transmit       8                  Bitmap

28                  XPDR Capabilities            2                  Table

30                  ADS-L Traffic Uplink         1                  Table
                    Client

31                  ADS-L FIS-B Client           1                  Table

32                  Reserved                     24
```

The individual fields are further explained in the following Sections.

#### ADS-L.4.SRD860.G.2.1 Max. Protocol Version

This field indicates the maximum protocol version the sender can support both as a receiver or transmitter for all payloads, thus allowing the sender to signal to its peers the true capabilities. The sender shall be capable of receiving and parsing payloads of all versions of the protocol up to and including the version indicated in this field. Also see ADS-L.4.SRD860.B.5 for a description of the Soft Versioning mechanism.

#### ADS-L.4.SRD860.G.2.2 M-Band Receive

This field declares the receive capabilities of the sender for ADS-L 4 SRD860 on the M-band for direct transmissions (air-to-air). The capability is defined in terms of average dwell time, corresponding to the average likelihood of receiving an arbitrary ADS-L message on M-band in the “Direct” time slot. Since M-Band comprises two channels for direct communication, a “Full (>80%)” capability is only achieved if the receiver is listening on both channels concurrently. A receiver listening on one of the two channels can only achieve “Partial” capability.

```
Receive Capabilities                                               Value

None                                                               0

Occasional (>5%)                                                   1

Partial (>40%)                                                     2

Full (>80%)                                                        3
```

#### ADS-L.4.SRD860.G.2.3 O-Band LDR Receive

This field declares the receive capabilities of the sender for ADS-L 4 SRD860 on the O-band LDR channel. The capability is defined in terms of average dwell time, corresponding to the average likelihood of receiving an arbitrary ADS-L message on O-band LDR in the “Direct” time slot.

```
Receive Capabilities                                               Value

None                                                               0

Occasional (>5%)                                                   1

Partial (>40%)                                                     2

Full (>80%)                                                        3
```

#### ADS-L.4.SRD860.G.2.4 O-Band HDR Receive

This field declares the receive capabilities of the sender for ADS-L 4 SRD860 on the O-band HDR channel. The capability is defined in terms of average dwell time, corresponding to the average likelihood of receiving an arbitrary ADS-L message on O-band HDR in the “Direct” time slot. Uplink capabilities on O-band are addressed in a different field and do not count here.

```
Receive Capabilities                                               Value

None                                                               0

Occasional (>5%)                                                   1

Partial (>40%)                                                     2

Full (>80%)                                                        3
```

#### ADS-L.4.SRD860.G.2.5 ADS-L 4 Mobile

This field declares the capabilities and the status of ADS-L 4 Mobile.

```
ADS-L 4 Mobile                                                      Value

None                                                                0

Initialized, but offline                                            1

Initialized and online                                              2

Reserved                                                            3
```

#### ADS-L.4.SRD860.G.2.6 E-conspicuity Receive

This field declares the receive capabilities of the sending aircraft with respect to other e-Conspicuity systems, either internal (integrated) to the ADS-L device, or external (additional equipment). This information may be used by ADS-L peers or ground infrastructure to optimize the ADS-L network, e.g. by not broadcasting on a specific frequency due to redundancy. The field is a bitmap, i.e. any combination of bits is applicable. This information shall include other systems installed in the same vehicle. When a particular capability is unknown, the bit shall not be set.

```
Receive Capabilities                                                Bit

FLARM                                                               0

PilotAware                                                          1

FANET                                                               2

OGN Tracker                                                         3

ADS-B 1090                                                          4

ADS-B UAT 978                                                       5

Mode-S / PCAS                                                       6

Reserved                                                            7
```

#### ADS-L.4.SRD860.G.2.7 E-conspicuity Transmit

This field declares the transmit capabilities of the sending aircraft with respect to other e-Conspicuity systems, either internal (integrated) to the ADS-L device, or external (additional equipment). This is a bitmap, i.e. any combination of bits is applicable. This information shall include other systems installed in the same vehicle. When a particular capability is unknown, the bit shall not be set.

```
Transmit Capabilities                                               Bit

FLARM                                                               0

PilotAware                                                          1

FANET                                                               2

OGN Tracker                                                         3

Transmit Capabilities                                                 Bit

ADS-B 1090                                                            4

ADS-B UAT 978                                                         5

Reserved                                                              6

Reserved                                                              7
```

#### ADS-L.4.SRD860.G.2.8 XPDR Capabilities

This field declares the installed transponder (XPDR) capabilities.

```
XPDR Capabilities                                                     Value

None / off / unknown                                                  0

Mode-C / ALT                                                          1

Mode-S                                                                2

Reserved                                                              3
```

#### ADS-L.4.SRD860.G.2.9 ADS-L Traffic Uplink Client

Flag, indicating the sender’s intent with respect to Traffic Uplink (traffic rebroadcasts) payloads.

```
ADS-L Traffic Uplink Client                                           Bit

Sender does not process Traffic Uplink payloads                       0

Sender processes Traffic Uplink payloads                              1
```

#### ADS-L.4.SRD860.G.2.10 ADS-L FIS-B Uplink Client

Flag, indicating the sender’s intent with respect to FIS-B payloads.

```
ADS-L FIS-B Client                                                    Bit

Sender does not process FIS-B Uplink payloads                         0

Sender processes FIS-B Uplink payloads                                1
```

#### ADS-L.4.SRD860.G.2.11 Transmit Rate

The minimum nominal transmission rate is once per 20 seconds.

### ADS-L.4.SRD860.G.3 Traffic Uplink Payload

This payload for presenting a situational awareness picture to any ADS-L client capable of processing it. It comprises up to 10 ADS-L targets in one radio packet. Target data may, for instance, originate from either of these systems:

```
•   ADS-B (1090 MHz) ground receiver
•   Mode-S ground station (SSR)
•   ADS-L 4 SRD 860
•   ADS-L 4 Mobile
•   RemoteID (direct broadcast or networked)
•   Other e-Conspicuity systems
```

The sender shall populate the fields at best effort, e.g. when constructing the sender address. The receiver shall consider the data with the necessary precaution considering deficiencies such as:

```
•   Large latency
•   Mismatch in the data fields (e.g. different semantics of accuracy fields)
•   Missing or wrong sender address
```

Up to 10 targets can be included in a single packet. The receiver shall determine the number of targets based on the Packet Length field (see Subpart D). The O-Band HDR channel and the Uplink time slot shall be used to transmit this payload. When forwarding ADS-L Traffic payloads, the payload must be copied verbatim, i.e. without any change. The age of the data, defined as the current time minus the Timestamp field of the Traffic payload, may not exceed 10 seconds. When forwarding traffic from systems other than ADS-L, data may be adjusted to semantically match the ADS-L Traffic payload, e.g. by sensor fusion or extrapolation. The Timestamp field shall be set according to Section ADS-L.4.SRD860.G.1.1. The age of the timestamp may not exceed 10 seconds and it may not be in the future.

```
Offset                    Description     Width (bits)              Encoding

0                         Traffic         30                        The address of the (original)
                          Address 1                                 sender, belonging to the Traffic
                                                                    payload, as defined in ADS-
                                                                    L.4.SRD860.F.2.2).

30                        Reserved        2

32                        Traffic         120                       The Traffic Payload, as defined
                          Payload 1                                 in ADS-L.4.SRD860.G.1)

152                       Traffic         30
                          Address 2

182                       Reserved        2

184                       Traffic         120

Offset                  Description    Width (bits)           Encoding

                        Payload 2

...                     ...            ...

152 * (N-1)             Traffic        30
                        Address N

152 * (N-1) + 30        Reserved       2

152 * (N-1) + 32        Traffic        120
                        Payload N

152 * N                 Padding        0..24                  Such that size of the payload in
                                                              bytes – 3 Bytes is divisible by
                                                              four.

                                                              See ADS-L.4.SRD860.F.2.5
```

### ADS-L.4.SRD860.G.4 FIS-B Uplink Payload

ADS-L supports the up-linking of FIS-B data by encapsulating the “UAT Ground Uplink Message” of UAT. The structure and content of the message are described in the RTCA standard DO-358B, Appendix A.1, and are fully adopted by ADS-L except for the following changes:

```
•   Instead of a fixed size of 432 bytes, the FIS-B Ground Uplink Payload is of variable size with an
    upper limited of 217 bytes (as determined by the maximum size of the ADS-L packet, headers and
    assuming FEC for error control).
•   The zero-fill padding used in DO-358B is omitted.
•   The following fields are considered reserved and shall be set to zeroes: TIS-B Site ID, Slot ID.
```

It is expected that the maximum payload size is sufficient for the vast majority of FIS-B Application Protocol Data Units (APDU) to be transmitted. In the (infrequent) case a single APDU is bigger than this size, then the APDU segmentation mechanism of DO-358B shall be used to transmit the APDU in multiple smaller segments. The O-Band HDR channel and the Uplink time slot shall be used to transmit this payload. The payload structure is given in the table below. For further reference, also see Figure A-1 in DO-358B.

```
Offset              Description                  Width (bits)        Encoding

0                   Radio Station Latitude       23                  See DO-358B, Section A.1.1.1
                    (WGS-84)

23                  Radio Station                24                  See DO-358B, Section A.1.1.1
                    Longitude (WGS-84)

47                  Position Valid               1                   See DO-358B, Section A.1.1.2

48                  UTC Coupled                  1                   See DO-358B, Section A.1.1.3

49                  Reserved                     1                   Set to zero

50                  App. Data Valid              1                   See DO-358B, Section A.1.1.5

51                  Reserved                     13                  Set to zero

64                  Application Data             Variable            See DO-358B, Section A.1.2
```

### ADS-L.4.SRD860.G.5 Remote Identification

This payload provision is for future use by drones / UAVs when so permitted by the requirements of the EU regulation on Unmanned Aircraft Systems. It is intended to encapsulate a Direct Remote Identification block message as specified in EN 4709-002:2023 "Civil Unmanned Aircraft Systems (UAS); Part 002: Direct Remote Identification" Further details and constraints on the payload encapsulation and transmission scheme will be provided in future versions.

## SUBPART H - CREDITS

This document was initially developed in 2022 by a group of industry stakeholders (the “working group”). Oversight and project management was provided by members of EASA, Credits are due to the following entities and persons:

```
•   Urban Mäder, FLARM Technology
•   Paweł Jałocha, Open Glider Network
•   Jürgen Eckert, Skytraxx
•   Ralf Heckhausen, AVIONIX ENGINEERING
```

## APPENDIX – PERFORMANCE INFORMATION

Airborne Transmitter

Nominal transmitter power

```
    -     M-Band – 12 dBm ≤ ERP ≤14 dBm (16 .. 25 mW)
    -     O-Band – 20 dBm ≤ ERP ≤27dBm (100 .. 500 mW)
Transmitter antenna
    -     Vertically polarized
    -     Omnidirectional
Antenna placement
    -     Suitable performance of device/system has been verified
Continuity
    -     The device should be designed, produced, and installed to ensure that a transmission failure is
          unlikely to occur during the device's total life, and indicatively with a probability per hour of use
          in the order of 1E-3 or less.
          Note: This target is under evaluation for the purposes of U-space airspace risk assessment, and
          may be revised in future standards.
Nominal position accuracy
    -     The nominal horizontal position accuracy (HFOM) is expected to be less than 30 m (i.e. a value of
          5 as per ADS-L.4.SRD 860.G.1.14).
    -     The nominal vertical position accuracy (VFOM) is expected to be less than 45 m (i.e. a value of 2
          as per ADS-L.4.SRD 860.G.1.15).
Latency
    -     The system should ensure that position and velocity data in the traffic payload are transmitted
          with a maximum latency of 0.5 seconds between measurement and broadcast.
```

Nominal Ground Receiver

Antenna

```
    -     Vertically polarized collinear
Antenna placement
    -     Unobstructed view of airspace to be monitored
    -     Interferences are managed (e.g. location with less RF sources, signal filtering)
Receiver
    -     Capable of receiving concurrently signals on defined frequencies
    -     Software Defined Radio recommended for future compatibility
```

Nominal Performance

Reception range

```
-   Expected range should be at least 10 km for an optimal system setup
    Note: The range value above applies to direct radio line-of-sight reception from an aircraft to a
    ground system. This range is typically longer than air-to-air reception, which may be shorter due
    to space, power, or other limitations if the device and antenna are installed on or inside an aircraft.
```

## APPENDIX – DECLARATION OF CONFORMITY

in accordance with ED Decision 2022/024/R and the attached Technical Specification for ADS-L transmissions using the SRD-860 frequency band (“ADS-L 4 SRD 860”)

The undersigned, representing the manufacturer:

```
        [Manufacturer Name and Address]
declares under sole responsibility that the following product:
        Product Name: [Product Name]
        Device P/N: [Part Number]
        Firmware Version: [Firmware Version]
is in conformity with the ADS-L 4 SRD860 Standard Issue 2
```

The device operates on the following transmission parameters:

```
        Transmission Bands and Power Levels: [Insert frequency bands within SRD860 and corresponding
        ERP/EIRP]
        Optional Parameters Transmitted (ADS-L): [List all optional ADS-L parameters that the device is
        capable of transmitting]
        Receive capabilities: [List band(s) received, average dwell time]

The manufacturer additionally declares that:
    -   Robust design processes and production controls are in place during manufacturing to ensure
        consistent quality and compliance with the applicable standard,
    -   The product meets all relevant EU regulatory requirements, including safety, EMC, and radio
        performance, and has been appropriately CE marked and documented,
    -   The product uses an approved address range (Section ADS-L.4.SRD860.F.2.2)
    -   The product nominally transmits at least the “Traffic” and “Status” payloads (Subpart G)
```

Date: [Date] [Authorized Representative Name] [Position] [Signature and Company Stamp]
