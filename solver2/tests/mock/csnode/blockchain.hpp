#pragma once

#include <csnode/WalletsCache.h>
#include <csdb/pool.h>

class BlockChain
{
public:
    using WalletData = Credits::WalletsCache::WalletData;

    BlockChain()
    {}

    BlockChain(const BlockChain &)
    {}

    MOCK_METHOD0(getLastWrittenHash, csdb::PoolHash&());
    MOCK_METHOD1(getHashBySequence, csdb::PoolHash&(uint32_t));
    MOCK_METHOD0(getLastWrittenSequence, size_t());
    MOCK_CONST_METHOD2(findWalletData, void(const csdb::Address::WalletId, WalletData&));

};
