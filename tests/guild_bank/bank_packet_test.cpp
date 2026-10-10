/*
 * WoW Compagnon, GNU GPL v2 or later.
 */
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;
constexpr uint8 GUILD_BANK_MAX_TABS = 6, GUILD_BANK_MAX_SLOTS = 98, MAX_GEM_SOCKETS = 3;
constexpr uint32 SMSG_GUILD_BANK_LIST = 1;
struct WorldSession { };
struct BankItem { uint8 tab, slot; uint32 entry, count; int32 withdrawals; };
struct WorldPacket
{
    uint32 opcode = SMSG_GUILD_BANK_LIST;
    std::vector<uint8> bytes;
    size_t position = 0;
    void rpos(size_t value) { position = value; }
    uint32 GetOpcode() const { return opcode; }
    template<class T> WorldPacket& operator>>(T& value)
    {
        if (position + sizeof(T) > bytes.size())
            throw std::runtime_error("truncated packet");
        std::memcpy(&value, bytes.data() + position, sizeof(T));
        position += sizeof(T);
        return *this;
    }
    WorldPacket& operator>>(std::string& value)
    {
        value.clear();
        uint8 ch;
        while ((*this >> ch), ch)
            value += char(ch);
        return *this;
    }
    template<class T> void Add(T value)
    {
        auto start = reinterpret_cast<uint8 const*>(&value);
        bytes.insert(bytes.end(), start, start + sizeof(T));
    }
};
struct ServerScript
{
    explicit ServerScript(char const*) { }
    virtual void OnPacketSent(WorldSession*, WorldPacket const&) { }
};
thread_local WorldSession* queriedSession = nullptr;
thread_local std::vector<BankItem>* queriedItems = nullptr;
#include "production_bank_packet.inc"

int main()
{
    WorldPacket packet;
    packet.Add(uint64(900)); packet.Add(uint8(0)); packet.Add(int32(-1)); packet.Add(uint8(1));
    packet.Add(uint8(1)); packet.Add(uint8('A')); packet.Add(uint8(0));
    packet.Add(uint8('B')); packet.Add(uint8(0)); packet.Add(uint8(2));
    packet.Add(uint8(4)); packet.Add(uint32(123)); packet.Add(int32(0)); packet.Add(int32(-7));
    packet.Add(int32(42)); packet.Add(int32(3)); packet.Add(int32(0)); packet.Add(uint8(0));
    packet.Add(uint8(1)); packet.Add(uint8(0)); packet.Add(int32(55));
    packet.Add(uint8(5)); packet.Add(uint32(0));
    WorldSession expected, unrelated;
    std::vector<BankItem> items;
    queriedSession = &expected;
    queriedItems = &items;
    BankPacketScript parser;
    parser.OnPacketSent(&unrelated, packet);
    assert(items.empty());
    parser.OnPacketSent(&expected, packet);
    assert(items.size() == 1 && items[0].entry == 123 && items[0].count == 3);
    assert(items[0].tab == 0 && items[0].slot == 4 && items[0].withdrawals == -1);
    items.clear();
    auto truncated = packet;
    truncated.bytes.pop_back();
    parser.OnPacketSent(&expected, truncated);
    assert(items.empty()); // A partial first item cannot survive a broken second item.
    auto partial = packet;
    partial.bytes[13] = 0;
    parser.OnPacketSent(&expected, partial);
    assert(items.empty());
    packet.opcode = 2;
    parser.OnPacketSent(&expected, packet);
    assert(items.empty());
    queriedItems = nullptr;
    std::cout << "PASS: real native snapshot parser, tabs, random properties, sockets, "
        "empty slots, session isolation and malformed packet rejection\n";
}
