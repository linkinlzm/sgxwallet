/*
    Copyright (C) 2019-Present SKALE Labs

    This file is part of sgxwallet.

    sgxwallet is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    sgxwallet is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with sgxwallet.  If not, see <https://www.gnu.org/licenses/>.

    @file LevelDB.cpp
    @author Stan Kladko
    @date 2019
*/

#include <stdexcept>
#include <memory>
#include <string>
#include <iostream>

#include "leveldb/db.h"
#include <jsonrpccpp/client.h>

#include "sgxwallet_common.h"
#include "SGXException.h"
#include "LevelDB.h"

#include "ServerInit.h"

#include "third_party/spdlog/spdlog.h"
#include "common.h"

using namespace leveldb;

static WriteOptions writeOptions;
static ReadOptions readOptions;

shared_ptr<string> LevelDB::readNewStyleValue(const string& value) {
    Json::Value key_data;
    Json::Reader reader;
    reader.parse(value.c_str(), key_data);

    return std::make_shared<string>(key_data["value"].asString());
}

std::shared_ptr<string> LevelDB::readString(const string &_key) {

    auto result = std::make_shared<string>();

    CHECK_STATE(db)

    auto status = db->Get(readOptions, _key, result.get());

    throwExceptionOnError(status);

    if (status.IsNotFound()) {
        return nullptr;
    }

    if (result->at(0) == '{') {
        return readNewStyleValue(*result);
    }

    return result;
}

void LevelDB::writeString(const string &_key, const string &_value) {
    Json::Value writerData;
    writerData["value"] = _value;
    writerData["timestamp"] = std::to_string(std::time(nullptr));

    Json::FastWriter fastWriter;
    std::string output = fastWriter.write(writerData);

    auto status = db->Put(writeOptions, Slice(_key), Slice(output));

    throwExceptionOnError(status);
}

void LevelDB::deleteDHDKGKey(const string &_key) {

    string full_key = "DKG_DH_KEY_" + _key;

    auto status = db->Delete(writeOptions, Slice(full_key));

    throwExceptionOnError(status);

}

void LevelDB::deleteTempNEK(const string &_key) {

    CHECK_STATE(_key.rfind("tmp_NEK", 0) == 0);

    auto status = db->Delete(writeOptions, Slice(_key));

    throwExceptionOnError(status);
}

void LevelDB::deleteKey(const string &_key) {

    auto status = db->Delete(writeOptions, Slice(_key));

    throwExceptionOnError(status);

}

void LevelDB::throwExceptionOnError(Status _status) {
    if (_status.IsNotFound())
        return;

    if (!_status.ok()) {
        throw SGXException(COULD_NOT_ACCESS_DATABASE, ("Could not access database database:" + _status.ToString()).c_str());
    }
}

uint64_t LevelDB::visitKeys(LevelDB::KeyVisitor *_visitor, uint64_t _maxKeysToVisit) {

    CHECK_STATE(_visitor);

    uint64_t readCounter = 0;

    shared_ptr<leveldb::Iterator> it( db->NewIterator(readOptions) );
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        _visitor->visitDBKey(it->key().data());
        readCounter++;
        if (readCounter >= _maxKeysToVisit) {
            break;
        }
    }

    return readCounter;
}

std::vector<string> LevelDB::writeKeysToVector1(uint64_t _maxKeysToVisit){
  uint64_t readCounter = 0;
  std::vector<string> keys;

  shared_ptr<leveldb::Iterator> it( db->NewIterator(readOptions) );
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    string cur_key(it->key().data(), it->key().size());
    keys.push_back(cur_key);
    readCounter++;
    if (readCounter >= _maxKeysToVisit) {
      break;
    }
  }

  return keys;
}

void LevelDB::writeDataUnique(const string & name, const string &value) {
  if (readString(name)) {
    spdlog::debug("Name {} already exists", name);
    throw SGXException(KEY_SHARE_ALREADY_EXISTS, "Data with this name already exists");
  }

  writeString(name, value);
}

pair<stringstream, uint64_t> LevelDB::getAllKeys() {
    stringstream keysInfo;

    leveldb::Iterator *it = db->NewIterator(readOptions);
    uint64_t counter = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        ++counter;
        string key = it->key().ToString();
        string value;
        if (it->value().ToString()[0] == '{') {
            // new style keys
            Json::Value key_data;
            Json::Reader reader;
            reader.parse(it->value().ToString().c_str(), key_data);

            string timestamp_to_date_command = "date -d @" + key_data["timestamp"].asString();
            value = " VALUE: " + key_data["value"].asString() + ", TIMESTAMP: " + exec(timestamp_to_date_command.c_str()) + '\n';
        } else {
            // old style keys
            value = " VALUE: " + it->value().ToString();
        }
        keysInfo << "KEY: " << key << ',' << value;
    }

    return {std::move(keysInfo), counter};
}

pair<string, uint64_t> LevelDB::getLatestCreatedKey() {
    leveldb::Iterator *it = db->NewIterator(readOptions);

    int64_t latest_timestamp = 0;
    string latest_created_key_name = "";
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        if (it->value().ToString()[0] == '{') {
            // new style keys
            Json::Value key_data;
            Json::Reader reader;
            reader.parse(it->value().ToString().c_str(), key_data);

            if (std::stoi(key_data["timestamp"].asString()) > latest_timestamp) {
                latest_timestamp = std::stoi(key_data["timestamp"].asString());
                latest_created_key_name = it->key().ToString();
            }
        } else {
            // old style keys
            // assuming server has at least one new-style key created
            continue;
        }
    }

    return {latest_created_key_name, latest_timestamp};
}


LevelDB::LevelDB(string &filename) {
    leveldb::Options options;
    options.create_if_missing = true;

    if (!leveldb::DB::Open(options, filename, (leveldb::DB **) &db).ok()) {
        throw std::runtime_error("Unable to open levelDB database");
    }

    if (db == nullptr) {
        throw std::runtime_error("Null levelDB object");
    }
}

LevelDB::~LevelDB() {
}

const std::shared_ptr<LevelDB> &LevelDB::getLevelDb() {
    CHECK_STATE(levelDb)
    return levelDb;
}

const std::shared_ptr<LevelDB> &LevelDB::getCsrDb() {
    CHECK_STATE(csrDb)
    return csrDb;
}

const std::shared_ptr<LevelDB> &LevelDB::getCsrStatusDb() {
    CHECK_STATE(csrStatusDb)
    return csrStatusDb;
}


std::shared_ptr<LevelDB> LevelDB::levelDb = nullptr;

std::shared_ptr<LevelDB> LevelDB::csrDb = nullptr;

std::shared_ptr<LevelDB> LevelDB::csrStatusDb = nullptr;

string LevelDB::sgx_data_folder;

bool LevelDB::isInited = false;

void LevelDB::initDataFolderAndDBs() {
    CHECK_STATE(!isInited)
    isInited = true;

    spdlog::info("Initing wallet database ... ");

    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        spdlog::error("Could not get current working directory.");
        throw SGXException(COULD_NOT_GET_WORKING_DIRECTORY, "Could not get current working directory.");
    }

    sgx_data_folder = string(cwd) + "/" + SGXDATA_FOLDER;

    struct stat info;
    if (stat(sgx_data_folder.c_str(), &info) !=0 ){
        spdlog::info("sgx_data folder does not exist. Creating ...");

        if (system(("mkdir " + sgx_data_folder).c_str()) == 0){
            spdlog::info("Successfully created sgx_data folder");
        }
        else{
            spdlog::error("Could not create sgx_data folder.");
            throw SGXException(ERROR_CREATING_SGX_DATA_FOLDER, "Could not create sgx_data folder.");
        }
    }

    spdlog::info("Opening wallet databases");

    auto dbName = sgx_data_folder +  WALLETDB_NAME;
    levelDb = make_shared<LevelDB>(dbName);

    auto csr_dbname = sgx_data_folder + "CSR_DB";
    csrDb = make_shared<LevelDB>(csr_dbname);

    auto csr_status_dbname = sgx_data_folder + "CSR_STATUS_DB";
    csrStatusDb = make_shared<LevelDB>(csr_status_dbname);

    spdlog::info("Successfully opened databases");
}

const string &LevelDB::getSgxDataFolder() {
    return sgx_data_folder;
}
