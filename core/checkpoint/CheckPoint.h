#pragma once

#include <set>
#include <string>

#include "common/DevInode.h"
#include "reader/LogFileReader.h"

namespace logtail {

class CheckPoint {
public:
    DevInode mDevInode;
    int64_t mOffset = 0;
    uint64_t mSignatureHash = 0;
    uint32_t mSignatureSize = 0;
    int64_t mLastUpdateTime = 0;
    bool mFileOpenFlag = false;
    bool mContainerStopped = false;
    bool mLastForceRead = false;
    std::string mCache;
    std::string mConfigName;
    std::string mFileName;
    std::string mRealFileName;
    int32_t mIdxInReaderArray = LogFileReader::CHECKPOINT_IDX_OF_NEW_READER_IN_ARRAY;

    std::string mLogMode4Path;
    std::string mFileKey;
    std::string mFileHeadMd5;
    int32_t mLogID;
    int32_t mPathID; 

    CheckPoint() {}

    CheckPoint(int64_t offset,
               const std::string & fileHeadMd5,
               DevInode devInode,
               const std::string & logMode4Path,
               const std::string & configName,
               const std::string & fileKey,
               const std::string & realFileName,
               int64_t lastUpdateTime,
               int32_t logID,
               int32_t pathID)
        : mDevInode(devInode),
          mOffset(offset),
          mLastUpdateTime(lastUpdateTime),
          mConfigName(configName),
          mRealFileName(realFileName),
          mLogMode4Path(logMode4Path),
          mFileKey(fileKey),
          mFileHeadMd5(fileHeadMd5),
          mLogID(logID),
          mPathID(pathID) {}

    CheckPoint(const std::string& filename,
               int64_t offset,
               uint32_t signatureSize,
               uint64_t signatureHash,
               DevInode devInode,
               const std::string& configName,
               const std::string& realFileName,
               bool fileOpenFlag,
               bool containerStopped,
               bool lastForceRead)
        : mDevInode(devInode),
          mOffset(offset),
          mSignatureHash(signatureHash),
          mSignatureSize(signatureSize),
          mLastUpdateTime(0),
          mFileOpenFlag(fileOpenFlag),
          mContainerStopped(containerStopped),
          mLastForceRead(lastForceRead),
          mConfigName(configName),
          mFileName(filename),
          mRealFileName(realFileName) {}
};

class DirCheckPoint {
public:
    std::string mParentName;
    std::set<std::string> mSubDir;
    int32_t mUpdateTime;
    DirCheckPoint() {}
    DirCheckPoint(const std::string& dir) : mParentName(dir), mUpdateTime(time(NULL)) {}
};

typedef std::shared_ptr<DirCheckPoint> DirCheckPointPtr;
typedef std::shared_ptr<CheckPoint> CheckPointPtr;

}
