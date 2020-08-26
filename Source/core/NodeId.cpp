 /*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
// SocketPortSettings.cpp: implementation of the NodeId class.
//
//////////////////////////////////////////////////////////////////////

#include "Portability.h"
#include "Serialization.h"
#include "TextFragment.h"
#include "Trace.h"
#include "string.h"

#ifdef __UNIX__
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#define ERRORRESULT errno
#endif

#ifdef __WINDOWS__
#include <Ws2tcpip.h>
#define ERRORRESULT ::WSAGetLastError()
#endif

#include "NodeId.h"

namespace WPEFramework {
namespace Core {

#ifndef __WINDOWS__
    static string NetlinkName(const NodeId::SocketInfo& input)
    {

        return (_T("Netlink:") + Core::NumberType<uint32_t>(input.NetlinkSocket.nl_destination).Text() + ':' + Core::NumberType<pid_t>(input.NetlinkSocket.nl_pid).Text() + ':' + Core::NumberType<pid_t>(input.NetlinkSocket.nl_groups).Text());
    }
    static string RawName(const NodeId::SocketInfo& input)
    {
        return (_T("Rawlink:") + Core::NumberType<int32_t>(input.RawSocket.sll_family).Text() + ':' + Core::NumberType<uint16_t>(input.RawSocket.sll_protocol).Text() + ':' + Core::NumberType<uint16_t>(input.RawSocket.sll_hatype).Text());
    }
#endif

#ifdef CORE_BLUETOOTH
    static string BTName(const NodeId::SocketInfo& input)
    {
        static TCHAR _hexArray[] = "0123456789ABCDEF";

        if (input.L2Socket.l2_type == BTPROTO_HCI) {
            return (_T("Bluetooth:") + Core::NumberType<uint16_t>(input.BTSocket.hci_dev).Text() + ':' + Core::NumberType<uint16_t>(input.BTSocket.hci_channel).Text());
        }
        TCHAR address[14];
        for (uint8_t index = 0; index < sizeof(input.L2Socket.l2_bdaddr); index++) {
            address[(index * 2)] = _hexArray[(input.L2Socket.l2_bdaddr.b[(sizeof(input.L2Socket.l2_bdaddr) - 1) - index] >> 4)];
            address[(index * 2) + 1] = _hexArray[(input.L2Socket.l2_bdaddr.b[(sizeof(input.L2Socket.l2_bdaddr) - 1) - index] & 0xF)];
        }
        address[12] = '\0';
        return (_T("BluetoothL2:") + Core::NumberType<uint16_t>(input.L2Socket.l2_cid).Text() + ':' + Core::NumberType<uint16_t>(input.L2Socket.l2_psm).Text() + ':' + Core::NumberType<pid_t>(input.L2Socket.l2_bdaddr_type).Text() + ':' + address);
    }
#endif

    /* static */ bool NodeId::m_isIPV6Enabled = true;

    //----------------------------------------------------------------------------
    // CONSTRUCTOR & DESTRUCTOR
    //----------------------------------------------------------------------------
    NodeId::NodeId()
        : m_hostName()
    {
        // Fill it with FF's
        memset(&m_structInfo, 0xFF, sizeof(m_structInfo));

        m_structInfo.IPV4Socket.sin_family = TYPE_EMPTY;
    }

    NodeId::NodeId(const struct sockaddr_in& rInfo)
        : m_hostName()
    {

        *this = rInfo;

        ASSERT(m_structInfo.IPV4Socket.sin_family == AF_INET);
    }

    NodeId::NodeId(const struct in_addr& value)
        : m_hostName()
    {

        struct sockaddr_in rInfo;
        ::memset(&rInfo, 0, sizeof(rInfo));
        rInfo.sin_family = AF_INET;
        rInfo.sin_addr = value;

        *this = rInfo;

        ASSERT(m_structInfo.IPV4Socket.sin_family == AF_INET);
    }
    NodeId::NodeId(const struct sockaddr_in6& rInfo)
        : m_hostName()
    {

        *this = rInfo;

        ASSERT(m_structInfo.IPV6Socket.sin6_family == AF_INET6);
    }

    NodeId::NodeId(const struct in6_addr& value)
        : m_hostName()
    {

        struct sockaddr_in6 rInfo;

        ::memset(&rInfo, 0, sizeof(rInfo));
        rInfo.sin6_family = AF_INET6;
        rInfo.sin6_addr = value;

        *this = rInfo;

        ASSERT(m_structInfo.IPV6Socket.sin6_family == AF_INET6);
    }
#ifndef __WINDOWS__
    NodeId::NodeId(const struct sockaddr_un& rInfo, const uint16_t access)
        : m_hostName(rInfo.sun_path)
    {

        *this = rInfo;
        m_structInfo.DomainSocket.un_access = access;

        ASSERT(m_structInfo.DomainSocket.sun_family == AF_UNIX);
    }
    NodeId::NodeId(const uint32_t destination, const pid_t pid, const uint32_t groups)
    {

        m_structInfo.NetlinkSocket.nl_family = AF_NETLINK;
        m_structInfo.NetlinkSocket.nl_pid = pid;
        m_structInfo.NetlinkSocket.nl_groups = groups;
        m_structInfo.NetlinkSocket.nl_destination = destination;

        m_hostName = NetlinkName(m_structInfo);
    }

    NodeId::NodeId(const struct sockaddr_ll& rInfo)
    {
        memcpy(&(m_structInfo.RawSocket), &rInfo, sizeof(sockaddr_ll));
        
        m_hostName = RawName(m_structInfo);
    }
    
    NodeId::NodeId(const int32_t interfaceIndex, const uint16_t protocolFilter, const uint16_t hardwareAddressLength, const uint8_t* hardwareAddress)
    {
        ASSERT(interfaceIndex > 0);

        m_structInfo.RawSocket.sll_family = AF_PACKET;
        m_structInfo.RawSocket.sll_ifindex = interfaceIndex;
        m_structInfo.RawSocket.sll_protocol = htons(protocolFilter);
        m_structInfo.RawSocket.sll_hatype = 0;
        
        if(hardwareAddressLength > 0){
            ASSERT(hardwareAddressLength == ETH_ALEN);
            m_structInfo.RawSocket.sll_halen = ETH_ALEN;
            memcpy(m_structInfo.RawSocket.sll_addr, hardwareAddress, (hardwareAddressLength <= ETH_ALEN) ? hardwareAddressLength : ETH_ALEN);
        }
    
        m_hostName = RawName(m_structInfo);
    }
#endif

#ifdef CORE_BLUETOOTH
    NodeId::NodeId(const uint16_t device, const uint16_t channel)
    {

        memset(&m_structInfo.BTSocket, 0, sizeof(m_structInfo.BTSocket));

        m_structInfo.BTSocket.hci_family = AF_BLUETOOTH;
        m_structInfo.BTSocket.hci_dev = device;
        m_structInfo.BTSocket.hci_channel = channel;
        m_structInfo.L2Socket.l2_type = BTPROTO_HCI;

        m_hostName = BTName(m_structInfo);
    }
    NodeId::NodeId(const bdaddr_t& address, const uint8_t addressType, const uint16_t cid, const uint16_t psm)
    {

        memset(&m_structInfo.L2Socket, 0, sizeof(m_structInfo.L2Socket));

        m_structInfo.L2Socket.l2_family = AF_BLUETOOTH;
        m_structInfo.L2Socket.l2_psm = htobs(psm);
        m_structInfo.L2Socket.l2_cid = htobs(cid);
        m_structInfo.L2Socket.l2_bdaddr_type = addressType;
        m_structInfo.L2Socket.l2_type = BTPROTO_L2CAP;

        ::memcpy(m_structInfo.L2Socket.l2_bdaddr.b, address.b, sizeof(m_structInfo.L2Socket.l2_bdaddr));

        m_hostName = BTName(m_structInfo);
    }
#endif

    NodeId::NodeId(
        const TCHAR strHostName[],
        const uint16_t nPortNumber,
        const enumType defaultType)
        : m_hostName()
    {

        m_structInfo.IPV4Socket.sin_family = TYPE_UNSPECIFIED;

        // Convert the name to an IP structure address
        Resolve(Core::TextFragment(strHostName), defaultType);

        ASSERT(nPortNumber < 0xFFFF);

        // Set the Port number used. (Struct of IPV4 and IPV6 are equeal w.r.t. port number)
        m_structInfo.IPV4Socket.sin_port = htons(nPortNumber);
    }

    NodeId::NodeId(
        const TCHAR strHostName[],
        const enumType defaultType)
        : m_hostName()
    {

        m_structInfo.IPV4Socket.sin_family = TYPE_UNSPECIFIED;

#ifndef __WINDOWS__
        if (strchr(strHostName, '/') != nullptr) {
            // Seems like we have a path, so a domain socket is required....
            const TCHAR* indicator;

            // There might be a ':0' at the end to identify the domain socket access properties. Extract these
            if ((indicator = strrchr(strHostName, '|')) != nullptr) {
                Core::NumberType<uint16_t> number(&(indicator[1]), strlen(&(indicator[1])));
                m_structInfo.DomainSocket.un_access = number.Value();
                m_hostName = string(strHostName, ((indicator - strHostName) / sizeof(TCHAR)));
            } else {
                m_structInfo.DomainSocket.un_access = static_cast<uint16_t>(~0);
                m_hostName = strHostName;
            }

            m_structInfo.DomainSocket.sun_family = AF_UNIX;
            strncpy(m_structInfo.DomainSocket.sun_path, m_hostName.c_str(), sizeof(m_structInfo.DomainSocket.sun_path));
            m_structInfo.DomainSocket.sun_path[sizeof(m_structInfo.DomainSocket.sun_path) - 1] = '\0';
        } else
#endif
        {
            const TCHAR* portNumber;
            const TCHAR* end;
            const TCHAR* start(strchr(strHostName, '['));

            // If a port number is mentioned on IPV6, the address is surrounded by square blocks, check for these block characters.
            if ((start != nullptr) && ((end = strrchr(strHostName, ']')) != nullptr)) {
                // This is a IPV6 address..
                Resolve(TextFragment(strHostName,

                            static_cast<uint32_t>(start - strHostName) + 1,
                            static_cast<uint32_t>((end - start) - 1) / static_cast<uint32_t>(sizeof(TCHAR))),
                    TYPE_IPV6);

                // See if there is a colon, the colon might follow a port numnber at the end..
                if ((portNumber = strrchr(&(end[1]), ':')) != nullptr) {
                    // Seems we have a number..
                    m_structInfo.IPV6Socket.sin6_port = htons(Core::NumberType<uint16_t>(&(portNumber[1]), static_cast<uint32_t>(strlen(&(portNumber[1])))).Value());
                }
            }
            // See if there is a colon, the colon might follow a port numnber at the end and make sure there is only 1 colon!!!
            else if (((portNumber = strrchr(strHostName, ':')) != nullptr) && (strchr(strHostName, ':') == portNumber)) {
                // Convert the name to an IP structure address
                Resolve(TextFragment(strHostName, 0, static_cast<uint32_t>(portNumber - strHostName) / static_cast<uint32_t>(sizeof(TCHAR))), defaultType);

                // Seems we have a number..
                if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
                    m_structInfo.IPV6Socket.sin6_port = htons(Core::NumberType<uint16_t>(&(portNumber[1]), static_cast<uint32_t>(strlen(&(portNumber[1])))).Value());
                } else if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
                    m_structInfo.IPV4Socket.sin_port = htons(Core::NumberType<uint16_t>(&(portNumber[1]), static_cast<uint32_t>(strlen(&(portNumber[1])))).Value());
                }

            } else {
                // Convert the name to an IP structure address
                Resolve(TextFragment(strHostName), defaultType);

                // Set the Port number used.
                m_structInfo.IPV4Socket.sin_port = 0;
            }
        }
    }

    NodeId::NodeId(const NodeId& rInfo)
    {

        *this = rInfo;
    }

    NodeId::NodeId(const NodeId& rInfo, const uint16_t portNumber)
    {
        *this = rInfo;
        PortNumber(portNumber);
    }

    //----------------------------------------------------------------------------
    // PUBLIC METHODS
    //----------------------------------------------------------------------------

    bool
    NodeId::operator==(const NodeId& rInfo) const
    {
        if (m_structInfo.IPV4Socket.sin_family == rInfo.m_structInfo.IPV4Socket.sin_family) {
            if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
                return ((m_structInfo.IPV4Socket.sin_addr.s_addr == rInfo.m_structInfo.IPV4Socket.sin_addr.s_addr) && (m_structInfo.IPV4Socket.sin_port == rInfo.m_structInfo.IPV4Socket.sin_port));
            } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {

#ifdef __WINDOWS__
                return ((memcmp(m_structInfo.IPV6Socket.sin6_addr.u.Byte, rInfo.m_structInfo.IPV6Socket.sin6_addr.u.Byte, sizeof(m_structInfo.IPV6Socket.sin6_addr.u.Byte)) == 0) && (m_structInfo.IPV6Socket.sin6_port == rInfo.m_structInfo.IPV6Socket.sin6_port));
#else
                return ((memcmp(m_structInfo.IPV6Socket.sin6_addr.s6_addr, rInfo.m_structInfo.IPV6Socket.sin6_addr.s6_addr, sizeof(m_structInfo.IPV6Socket.sin6_addr.s6_addr)) == 0) && (m_structInfo.IPV6Socket.sin6_port == rInfo.m_structInfo.IPV6Socket.sin6_port));
#endif
            }
#ifndef __WINDOWS__
            else if (m_structInfo.DomainSocket.sun_family == AF_UNIX) {
                return (strcmp(m_structInfo.DomainSocket.sun_path, rInfo.m_structInfo.DomainSocket.sun_path) == 0);
            } else if (m_structInfo.DomainSocket.sun_family == AF_NETLINK) {
                return ((m_structInfo.NetlinkSocket.nl_destination == rInfo.m_structInfo.NetlinkSocket.nl_destination) && (m_structInfo.NetlinkSocket.nl_pid == rInfo.m_structInfo.NetlinkSocket.nl_pid) && (m_structInfo.NetlinkSocket.nl_groups == rInfo.m_structInfo.NetlinkSocket.nl_groups));
            }
#endif
#ifdef CORE_BLUETOOTH
            else if (m_structInfo.DomainSocket.sun_family == AF_BLUETOOTH) {
                if (m_structInfo.L2Socket.l2_type == rInfo.m_structInfo.L2Socket.l2_type) {
                    if (m_structInfo.L2Socket.l2_type == BTPROTO_HCI) {
                        return ((m_structInfo.BTSocket.hci_channel == rInfo.m_structInfo.BTSocket.hci_channel) && (m_structInfo.BTSocket.hci_dev == rInfo.m_structInfo.BTSocket.hci_dev));
                    } else {
                        return ((m_structInfo.L2Socket.l2_cid == rInfo.m_structInfo.L2Socket.l2_cid) && (m_structInfo.L2Socket.l2_bdaddr_type == rInfo.m_structInfo.L2Socket.l2_bdaddr_type) && (::memcmp(m_structInfo.L2Socket.l2_bdaddr.b, rInfo.m_structInfo.L2Socket.l2_bdaddr.b, sizeof(m_structInfo.L2Socket.l2_bdaddr)) == 0));
                    }
                }
            }
#endif
        }

        return (false);
    }

    NodeId&
    NodeId::operator=(const NodeId& rInfo)
    {

        // Copy the struct info
        memcpy(&m_structInfo, &rInfo.m_structInfo, sizeof(m_structInfo));

        m_hostName = rInfo.m_hostName;

        // Give back our-selves.
        return (*this);
    }

    NodeId&
    NodeId::operator=(const struct sockaddr_in& rInfo)
    {

        // Copy the struct info
        memcpy(&m_structInfo.IPV4Socket, &rInfo, sizeof(m_structInfo.IPV4Socket));

        m_hostName.clear();

        // Give back our-selves.
        return (*this);
    }

    NodeId&
    NodeId::operator=(const struct sockaddr_in6& rInfo)
    {

        // Copy the struct info
        memcpy(&m_structInfo.IPV6Socket, &rInfo, sizeof(m_structInfo.IPV6Socket));

        m_hostName.clear();

        // Give back our-selves.
        return (*this);
    }

#ifndef __WINDOWS__
    NodeId&
    NodeId::operator=(const struct sockaddr_un& rInfo)
    {
        // Copy the struct info
        memcpy(&m_structInfo.DomainSocket, &rInfo, sizeof(m_structInfo.DomainSocket));

        m_hostName = rInfo.sun_path;

        // Give back our-selves.
        return (*this);
    }

    NodeId&
    NodeId::operator=(const struct sockaddr_nl& rInfo)
    {
        // Copy the struct info
        memcpy(&m_structInfo.NetlinkSocket, &rInfo, sizeof(struct sockaddr_nl));

        m_structInfo.NetlinkSocket.nl_destination = 0;
        m_hostName = NetlinkName(m_structInfo);

        // Give back our-selves.
        return (*this);
    }

    NodeId&
    NodeId::operator=(const struct sockaddr_ll& rInfo)
    {
        // Copy the struct info
        memcpy(&m_structInfo.RawSocket, &rInfo, sizeof(struct sockaddr_ll));

        m_hostName = RawName(m_structInfo);

        // Give back our-selves.
        return (*this);
    }
#endif

#ifdef CORE_BLUETOOTH
    NodeId&
    NodeId::operator=(const struct sockaddr_hci& rInfo)
    {
        // Copy the struct info
        memcpy(&m_structInfo.BTSocket, &rInfo, sizeof(struct sockaddr_hci));

        m_structInfo.L2Socket.l2_type = BTPROTO_HCI;
        m_hostName = BTName(m_structInfo);

        // Give back our-selves.
        return (*this);
    }

    NodeId&
    NodeId::operator=(const struct sockaddr_l2& rInfo)
    {
        // Copy the struct info
        memcpy(&m_structInfo.L2Socket, &rInfo, sizeof(struct sockaddr_l2));

        m_structInfo.L2Socket.l2_type = BTPROTO_L2CAP;
        m_hostName = BTName(m_structInfo);

        // Give back our-selves.
        return (*this);
    }
#endif

    NodeId&
    NodeId::operator=(const union SocketInfo& rInfo)
    {

        // Copy the struct info
        memcpy(&m_structInfo, &rInfo, sizeof(m_structInfo));

#ifndef __WINDOWS__
        if (m_structInfo.NetlinkSocket.nl_family == AF_NETLINK) {
            m_hostName = NetlinkName(m_structInfo);
        } else if (m_structInfo.DomainSocket.sun_family == AF_UNIX) {
            m_hostName = m_structInfo.DomainSocket.sun_path;
        }
#ifdef CORE_BLUETOOTH
        else if (m_structInfo.BTSocket.hci_family == AF_BLUETOOTH) {
            m_hostName = BTName(m_structInfo);
        }
#endif
        else {
            m_hostName.clear();
        }
#else
#ifdef CORE_BLUETOOTH
        if (m_structInfo.BTSocket.hci_family == AF_BLUETOOTH) {
            m_hostName = BTName(m_structInfo);
            else
#endif
                m_hostName.clear();
#endif

        // Give back our-selves.
        return (*this);
    }

    bool NodeId::IsMulticast() const
    {
        if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
#ifdef __WINDOWS__
            return (((m_structInfo.IPV4Socket.sin_addr.S_un.S_un_b.s_b1 & 0xF0) & 0xE0) == 0xE0);
#else
                return ((htonl(m_structInfo.IPV4Socket.sin_addr.s_addr) & 0xE0000000) == 0xE0000000);
#endif
        } else if (m_structInfo.IPV4Socket.sin_family == AF_INET6) {
            return (false);
        }

        return (false);
    }

    string
    NodeId::QualifiedName() const
    {
        if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
            Core::NumberType<uint16_t, false, BASE_DECIMAL> convertedNumber(ntohs(m_structInfo.IPV4Socket.sin_port));

            return (HostName() + _T(":") + convertedNumber.Text());
        } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
            Core::NumberType<uint16_t, false, BASE_DECIMAL> convertedNumber(ntohs(m_structInfo.IPV6Socket.sin6_port));

            return (_T("[") + HostName() + _T("]:") + convertedNumber.Text());
        }

        return (m_hostName);
    }

    string
    NodeId::HostName() const
    {
        if ((IsValid() == true) && (m_hostName.empty() == true)) {
            char host[NI_MAXHOST], service[NI_MAXSERV];

            int error = getnameinfo(
                reinterpret_cast<const struct sockaddr*>(&(m_structInfo.IPV4Socket)), Size(),
                host, sizeof(host),
                service, sizeof(service), NI_NUMERICSERV);

            if (error == 0) {
                m_hostName = ToString(host);
            } else {
                TRACE_L1("Function getnameinfo failed, error: %s", gai_strerror(error));
            }

            if (m_hostName.empty() == true) {
                m_hostName = HostAddress();
            }
        }

        return (m_hostName);
    }

    string
    NodeId::HostAddress() const
    {
        string result;

        if ((m_structInfo.IPV4Socket.sin_family == AF_UNIX) || (m_structInfo.IPV4Socket.sin_family == AF_NETLINK)) {
            result = m_hostName;
        } else if (IsValid() == true) {
            if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
                uint32_t nAddress = m_structInfo.IPV4Socket.sin_addr.s_addr;

                TCHAR identifier[16];

#ifdef __WINDOWS__
#pragma warning(disable : 4996)
#endif

                // Lets resolve it four digits.
                _stprintf(identifier, _T("%d.%d.%d.%d"), ((nAddress & 0x000000FF) >> 0),
                    ((nAddress & 0x0000FF00) >> 8),
                    ((nAddress & 0x00FF0000) >> 16),
                    ((nAddress & 0xFF000000) >> 24));

#ifdef __WINDOWS__
#pragma warning(default : 4996)
#endif

                result = string(identifier);
            } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
                char buffer[INET6_ADDRSTRLEN];
                if (getnameinfo(*this, Size(), buffer, sizeof(buffer), 0, 0, NI_NUMERICHOST) == 0) {
                    result = string(buffer);
                }
            }
        }
        return (result);
    }

    NodeId
    NodeId::AnyInterface() const
    {
        Core::NodeId result;

        switch (Type()) {
        case TYPE_IPV4:
            result.m_structInfo.IPV4Socket.sin_family = AF_INET;
            result.m_structInfo.IPV4Socket.sin_addr.s_addr = 0;
            result.m_structInfo.IPV4Socket.sin_port = 0;
            break;
        case TYPE_IPV6:
            result.m_structInfo.IPV6Socket.sin6_family = AF_INET6;
            result.m_structInfo.IPV6Socket.sin6_port = 0;
            memset(result.m_structInfo.IPV6Socket.sin6_addr.s6_addr, 0, sizeof(result.m_structInfo.IPV6Socket.sin6_addr.s6_addr));
            break;
#ifndef __WINDOWS__
        case TYPE_DOMAIN:
            result.m_structInfo.DomainSocket.sun_family = AF_UNIX;
            result.m_structInfo.DomainSocket.sun_path[0] = '\0';
            break;
        case TYPE_NETLINK:
            result.m_structInfo.NetlinkSocket.nl_family = AF_NETLINK;
            result.m_structInfo.NetlinkSocket.nl_destination = 0;
            result.m_structInfo.NetlinkSocket.nl_pid = 0;
            result.m_structInfo.NetlinkSocket.nl_groups = 0;
            break;
#endif

        default:
            break;
        }

        return (result);
    }
    bool
    NodeId::IsAnyInterface() const
    {
        if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
            return (m_structInfo.IPV4Socket.sin_addr.s_addr == 0);
        } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
            uint8_t index = sizeof(m_structInfo.IPV6Socket.sin6_addr.s6_addr);
            while (index > 0) {
                if (m_structInfo.IPV6Socket.sin6_addr.s6_addr[index - 1] != 0) {
                    break;
                }
                index--;
            }
            return (index == 0);
        }
#ifndef __WINDOWS__
        else if (m_structInfo.DomainSocket.sun_family == AF_UNIX) {
            return (m_structInfo.DomainSocket.sun_path[0] == '\0');
        } else if (m_structInfo.NetlinkSocket.nl_family == AF_NETLINK) {
            return ((m_structInfo.NetlinkSocket.nl_destination == 0) && (m_structInfo.NetlinkSocket.nl_pid == 0) && (m_structInfo.NetlinkSocket.nl_groups == 0));
        }
#endif

        return (false);
    }

    bool
    NodeId::IsLocalInterface() const
    {
        if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
            return ((htonl(m_structInfo.IPV4Socket.sin_addr.s_addr) & 0xFF000000) == 0x7F000000);
        } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
            uint8_t index = sizeof(m_structInfo.IPV6Socket.sin6_addr.s6_addr) - 1;

            if (m_structInfo.IPV6Socket.sin6_addr.s6_addr[index] == '1') {

                while (index > 0) {
                    if (m_structInfo.IPV6Socket.sin6_addr.s6_addr[index - 1] != 0) {
                        break;
                    }
                    index--;
                }
            }
            return (index == 0);
        }
#ifndef __WINDOWS__
        else if ((m_structInfo.DomainSocket.sun_family == AF_UNIX) || (m_structInfo.NetlinkSocket.nl_family == AF_NETLINK)) {
            return (true);
        }
#endif
        return (false);
    }

    NodeId
    NodeId::Origin() const
    {

        NodeId response;

        if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
            response.m_structInfo.IPV4Socket.sin_family = AF_INET;
            response.m_structInfo.IPV4Socket.sin_addr.s_addr = 0;
            response.m_structInfo.IPV4Socket.sin_port = 0;
            response.m_hostName.clear();
        } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
            response.m_structInfo.IPV6Socket.sin6_family = AF_INET;
            response.m_structInfo.IPV4Socket.sin_addr.s_addr = 0;
            response.m_structInfo.IPV6Socket.sin6_port = 0;

            response.m_hostName.clear();
        }
#ifndef __WINDOWS__
        else if (m_structInfo.DomainSocket.sun_family == AF_UNIX) {
            struct sockaddr_un info;
            memset(&info, 0x00, sizeof(info));
            info.sun_family = AF_UNIX;
            response = info;
        } else {
            struct sockaddr_nl info;
            info.nl_family = AF_NETLINK;
            info.nl_pid = 0;
            info.nl_groups = 0;
            response = info;
        }
#endif

        return (response);
    }

    uint8_t NodeId::DefaultMask() const
    {

        uint8_t result = ~0;

        if (m_structInfo.IPV4Socket.sin_family == AF_INET) {
            unsigned long l_Address = m_structInfo.IPV4Socket.sin_addr.s_addr;
            if ((l_Address & 0x80000000L) == 0) {
                result = 24;
            } else if ((l_Address & 0x40000000L) == 0) {
                result = 16;
            } else {
                result = 8;
            }

        } else if (m_structInfo.IPV6Socket.sin6_family == AF_INET6) {
            result = 48;
        }

        return (result);
    }

    //----------------------------------------------------------------------------
    // PROTECTED METHODS
    //----------------------------------------------------------------------------

    void
    NodeId::Resolve(const TextFragment& strHostName, const enumType defaultType)
    {

        if (IsValid() == false) {

            struct addrinfo hints;
            struct addrinfo* result = nullptr;
            std::string text;
            ToString(strHostName.Text().c_str(), text);

            memset(&hints, 0, sizeof(struct addrinfo));
            hints.ai_family = (m_isIPV6Enabled == false ? AF_INET : defaultType); /* Allow IPv4 or IPv6 */
            hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
            hints.ai_flags = AI_PASSIVE; /* For wildcard IP address */
            hints.ai_protocol = IPPROTO_TCP; /* Only TCP protocol */

            int error = getaddrinfo(text.c_str(), nullptr, &hints, &result);

            if ((error == 0) && (result != nullptr)) {

                /* getaddrinfo() returns a list of address structures. */
                /* Jut pick the first one.. */
                memcpy(&(m_structInfo.IPV4Socket), result->ai_addr, (result->ai_family == AF_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)));
                m_structInfo.IPV4Socket.sin_family = result->ai_family;

            } else if (error == -2) {
                m_structInfo.IPV4Socket.sin_family = TYPE_EMPTY;
            } else {
                TRACE_L1("Function ::getaddrinfo() for %s failed, error %s", text.c_str(), gai_strerror(error));
                m_structInfo.IPV4Socket.sin_family = TYPE_UNSPECIFIED;
            }
            if (result != nullptr)
                freeaddrinfo(result);
        }
    }

    NodeId IPNode::Broadcast() const
    {

        NodeId result(*this);
        uint8_t mask = 0;
        uint8_t* address = nullptr;

        if (Type() == Core::NodeId::TYPE_IPV4) {
            mask = 32 - Mask();
            struct sockaddr_in* space = reinterpret_cast<struct sockaddr_in*>(static_cast<struct sockaddr*>(result));
            address = &(reinterpret_cast<uint8_t*>(&(space->sin_addr.s_addr))[3]);
        } else if (Type() == Core::NodeId::TYPE_IPV6) {
            mask = 128 - Mask();
            struct sockaddr_in6* space = reinterpret_cast<struct sockaddr_in6*>(static_cast<struct sockaddr*>(result));
            address = &(space->sin6_addr.s6_addr[15]);
        }

        if (address != nullptr) {
            while (mask >= 8) {
                *address-- = 0xFF;
                mask -= 8;
            }

            if (mask > 0) {
                *address |= ((1 << mask) - 1);
            }
        }

        return (result);
    }
}
}
