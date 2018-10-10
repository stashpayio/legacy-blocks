// Copyright (c) 2018 The Stash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stash.h"
#include <stdio.h>
#include "chain.h"
#include "validation.h"
#include "script/standard.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "uint256.h"
#include "base58.h"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include "utilstrencodings.h"
#include "util.h"

bool isEmpty(const std::vector<bool>& vout) {
  for (unsigned int i = 0; i < vout.size(); i++) {
    if (vout[i]) {
      return false;
    }
  }
  return true;
}

bool isFull(const std::vector<bool>& vout) {
  for (unsigned int i = 0; i < vout.size(); i++) {
    if (!vout[i]) {
      return false;
    }
  }
  return true;
}

int bitCount(const std::vector<bool>& vout) {
  int count = 0;
  for (unsigned int i = 0; i < vout.size(); i++) {
    if (vout[i]) {
      count++;
    }
  }
  return count;
}

const std::string bits(const std::vector<bool>& vout) {
  std::stringstream buffer;
  for (unsigned int i = 0; i < vout.size(); i++) {
    buffer << (vout[i] ? '1' : '0');
  }
  return buffer.str();
}

class TransactionDetails {

public:

  TransactionDetails() : blockIndex(0), txIndex(0), vout(0) {
   }

  TransactionDetails(unsigned int block,unsigned int tx, std::vector<bool>::size_type utxoCount)
      : blockIndex(block),txIndex(tx), vout(utxoCount,true) {
  }

  TransactionDetails(const TransactionDetails& other)
      : blockIndex(other.blockIndex), txIndex(other.txIndex), vout(other.vout) {
  }

  unsigned int blockIndex;
  unsigned int txIndex;
  std::vector<bool> vout;
};

int totalNumberOfBlocks = 0;
std::map<uint256,TransactionDetails> txMap;
std::map<std::string,CAmount> totalAmounts;
std::vector<std::set<uint256>> blockTxs;

void printMap(unsigned int block) {
  for (const auto& pair : txMap) {
    if (pair.second.blockIndex == block) {
      printf("%s : %d.%d : %lu(%s)\n",pair.first.GetHex().c_str(),pair.second.blockIndex,pair.second.txIndex,pair.second.vout.size(),bits(pair.second.vout).c_str());
    }
  }
}

void spendUTXO(const COutPoint& utxo) {
  auto entry = txMap.find(utxo.hash);
  if (entry == txMap.end()) {
    printf("Missing tx: %s\n",utxo.hash.GetHex().c_str());
    return;
  }
  entry->second.vout[utxo.n] = false;
  if (isEmpty(entry->second.vout)) {
    txMap.erase(utxo.hash);
  }
}

void processTransaction(const CTransactionRef& tx,unsigned int blockIndex,unsigned int txIndex) {
  if (txMap.find(tx->GetHash()) != txMap.end()) {
    printf("shit\n");
  }
  // inputs
  if (!tx->IsCoinBase()) {
    for (unsigned int i = 0; i < tx->vin.size(); i++) {
      spendUTXO(tx->vin[i].prevout);
    }
  }

  // outputs
  auto ret = txMap.insert(std::make_pair(tx->GetHash(),TransactionDetails(blockIndex,txIndex,tx->vout.size())));
  if (!ret.second) {
    printf("Duplicate hash: %s\n",tx->GetHash().GetHex().c_str());
  }
}

void processBlock(unsigned int blockIndex) {
  CBlockIndex* index = chainActive[blockIndex];
  CBlock block;
  if (!ReadBlockFromDisk(block,index,Params().GetConsensus())) {
    printf("  Failed to read block at height: %d\n",blockIndex);
    return;
  }
  for (unsigned int k = 0; k < block.vtx.size(); k++) {
    CTransactionRef tx = block.vtx[k];
    processTransaction(tx,blockIndex,k);
  }
}

int countUTXOs() {
  int count = 0;
  for (const auto& pair : txMap) {
    count += bitCount(pair.second.vout);
  }
  return count;
}

void outputUTXOsForTx(std::ofstream& stream,const std::pair<uint256,TransactionDetails>& details) {
  CBlockIndex* index = chainActive[details.second.blockIndex];
  CBlock block;
  if (!ReadBlockFromDisk(block,index,Params().GetConsensus())) {
    printf("  Failed to read block at height: %d\n",details.second.blockIndex);
    return;
  }
  const auto& tx = block.vtx[details.second.txIndex];
  for (unsigned int i = 0; i < tx->vout.size(); i++) {
    if (details.second.vout[i]) {
      const auto& utxo = tx->vout[i];
      stream  //<< details.first.GetHex() << '\t'
              << details.second.blockIndex << '\t'
              << details.second.txIndex << '\t'
              << i << '\t'
              << utxo.nValue << '\t'
              << HexStr(utxo.scriptPubKey.begin(), utxo.scriptPubKey.end()) << '\t'
              << utxo.nRounds << std::endl;
    }
  }

}

void exportUTXOs() {
  std::string filename = GetArg("-utxofile",Params().NetworkIDString() + "net_utxo.csv");
  std::ofstream ss(filename);
  if (!ss.is_open()) {
    printf("Failed to open export file: %s\n",filename.c_str());
    return;
  }
  for (const auto& pair : txMap) {
    outputUTXOsForTx(ss,pair);
  }
  ss.close();
}

void outputAddress(std::ofstream& stream,const std::pair<std::string,CAmount>& details) {
  if (details.second > 0) {
    stream  << details.first << '\t'
            << details.second
            << std::endl;
  }
}


void exportAddresses() {
  std::string filename = GetArg("-addressfile",Params().NetworkIDString() + "net_address.csv");
  std::ofstream ss(filename);
  if (!ss.is_open()) {
    printf("Failed to open export file: %s\n",filename.c_str());
    return;
  }
  for (const auto& pair : totalAmounts) {
    outputAddress(ss,pair);
  }
  ss.close();
}

void recordTransactionForBlock(const uint256& hash,int blockIndex) {
  blockTxs[blockIndex].insert(hash);
}

void createBlockTxs() {
  for (auto it = txMap.begin(); it != txMap.end(); it++) {
    recordTransactionForBlock(it->first,it->second.blockIndex);
  }

}


void addOneUTXOAmount(int blockIndex,const std::set<uint256>& hashes) {
  CBlockIndex* index = chainActive[blockIndex];
  CBlock block;
  if (!ReadBlockFromDisk(block,index,Params().GetConsensus())) {
    printf("  Failed to read block at height: %d\n",blockIndex);
    return;
  }

  for (auto it = hashes.cbegin();it != hashes.cend();it++) {
    auto entry = txMap.find(*it);
    const auto& tx = block.vtx[entry->second.txIndex];
    for (unsigned int i = 0; i < tx->vout.size(); i++) {
      if (entry->second.vout[i]) {
        const auto& utxo = tx->vout[i];
        txnouttype type;
        std::vector<CTxDestination> addresses;
        int nRequired;

        if (!ExtractDestinations(utxo.scriptPubKey, type, addresses, nRequired)) {
        //////  printf("Type: %s\n",GetTxnOutputType(type));
          entry->second.vout[i] = false;
          continue;
        }

        if (addresses.size() != 1) {
        ////// printf("Address count: %lu (%s)\n",addresses.size(),GetTxnOutputType(type));
          continue;
        }

        if (type == TX_PUBKEY || type ==  TX_PUBKEYHASH || type == TX_SCRIPTHASH) {
          std::string addr = CBitcoinAddress(addresses[0]).ToString();
          ///printf("Address: %s\n",addr.c_str());

          auto e = totalAmounts.find(addr);
          if (e == totalAmounts.end()) {
            totalAmounts[addr] = utxo.nValue;
          } else {
            totalAmounts[addr] += utxo.nValue;
          }
          entry->second.vout[i] = false;
        }
      }
    }
    if (isEmpty(entry->second.vout)) {
       txMap.erase(*it);
    }
  }
}

void calculateAmounts() {
  int count = 0;
  for (int i = 1; i <= totalNumberOfBlocks; i++) {
    addOneUTXOAmount(i,blockTxs[i]);
    count++;
    if (count % 50000 == 0) {
      printf("Count = %d\n",count);
    }
  }
 }

void processAllBlocks() {
  printf("Processing blocks\n");

  for (int i = 1; i <= totalNumberOfBlocks; i++) {
    if (i % 50000 == 0) {
      printf("Block: %u; Transactions: %lu\n",i,txMap.size());
    }
    processBlock(i);
  }
  printf("Number of transactions: %lu\n",txMap.size());
  printf("Number of UTXO: %d\n",countUTXOs());
  createBlockTxs();
  int numberOfTransactions = 0;
  for (int i = 1; i <= totalNumberOfBlocks; i++) {
    numberOfTransactions += blockTxs[i].size();
  }
  printf("Number of transactions: %d\n",numberOfTransactions);
  calculateAmounts();
  printf("Number of addresses: %lu\n",totalAmounts.size());
  printf("Number of UTXO: %d\n",countUTXOs());
  exportUTXOs();
  exportAddresses();
}

void processForStash() {
  totalNumberOfBlocks =  GetArg("-legacyheight",chainActive.Height());
  printf("Height exported: %d\n",totalNumberOfBlocks);
  std::set<uint256> empty;
  for (int i = 0; i <= totalNumberOfBlocks; i++) {
    blockTxs.push_back(empty);
  }
  processAllBlocks();
}
