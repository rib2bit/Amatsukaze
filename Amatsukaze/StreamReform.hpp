/**
* Output stream construction
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <time.h>

#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "StreamUtils.hpp"

// 時間は全て 90kHz double で計算する
// 90kHzでも60*1000/1001fpsの1フレームの時間は整数で表せない
// だからと言って27MHzでは数値が大きすぎる

struct FileAudioFrameInfo : public AudioFrameInfo {
	int audioIdx;
	int codedDataSize;
	int waveDataSize;
	int64_t fileOffset;
	int64_t waveOffset;

	FileAudioFrameInfo()
		: AudioFrameInfo()
		, audioIdx(0)
		, codedDataSize(0)
		, waveDataSize(0)
		, fileOffset(0)
		, waveOffset(-1)
	{ }

	FileAudioFrameInfo(const AudioFrameInfo& info)
		: AudioFrameInfo(info)
		, audioIdx(0)
		, codedDataSize(0)
		, waveDataSize(0)
		, fileOffset(0)
		, waveOffset(-1)
	{ }
};

struct FileVideoFrameInfo : public VideoFrameInfo {
	int64_t fileOffset;

	FileVideoFrameInfo()
		: VideoFrameInfo()
		, fileOffset(0)
	{ }

	FileVideoFrameInfo(const VideoFrameInfo& info)
		: VideoFrameInfo(info)
		, fileOffset(0)
	{ }
};

enum StreamEventType {
	STREAM_EVENT_NONE = 0,
	PID_TABLE_CHANGED,
	VIDEO_FORMAT_CHANGED,
	AUDIO_FORMAT_CHANGED
};

struct StreamEvent {
	StreamEventType type;
	int frameIdx;	// フレーム番号
	int audioIdx;	// 変更された音声インデックス（AUDIO_FORMAT_CHANGEDのときのみ有効）
	int numAudio;	// 音声の数（PID_TABLE_CHANGEDのときのみ有効）
};

typedef std::vector<std::vector<int>> FileAudioFrameList;

struct OutVideoFormat {
	int formatId; // 内部フォーマットID（通し番号）
	int videoFileId;
	VideoFormat videoFormat;
	std::vector<AudioFormat> audioFormat;
};

// 音ズレ統計情報
struct AudioDiffInfo {
	double sumPtsDiff;
	int totalSrcFrames;
	int totalAudioFrames; // 出力した音声フレーム（水増し分を含む）
	int totalUniquAudioFrames; // 出力した音声フレーム（水増し分を含まず）
	double maxPtsDiff;
	double maxPtsDiffPos;
	double basePts;

	// 秒単位で取得
	double avgDiff() const {
		return ((double)sumPtsDiff / totalAudioFrames) / MPEG_CLOCK_HZ;
	}
	// 秒単位で取得
	double maxDiff() const {
		return (double)maxPtsDiff / MPEG_CLOCK_HZ;
	}

	void printAudioPtsDiff(AMTContext& ctx) const {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;

		ctx.infoF("出力音声フレーム: %d（うち水増しフレーム%d）",
			totalAudioFrames, totalAudioFrames - totalUniquAudioFrames);
		ctx.infoF("未出力フレーム: %d（%.3f%%）",
			notIncluded, (double)notIncluded * 100 / totalSrcFrames);

		ctx.infoF("音ズレ: 平均 %.2fms 最大 %.2fms",
			avgDiff, maxDiff);
		if (maxPtsDiff > 0 && maxDiff - avgDiff > 1) {
			double sec = elapsedTime(maxPtsDiffPos);
			int minutes = (int)(sec / 60);
			sec -= minutes * 60;
			ctx.infoF("最大音ズレ位置: 入力最初の映像フレームから%d分%.3f秒後",
				minutes, sec);
		}
	}

	void printToJson(StringBuilder& sb) {
		double avgDiff = this->avgDiff() * 1000;
		double maxDiff = this->maxDiff() * 1000;
		int notIncluded = totalSrcFrames - totalUniquAudioFrames;
		double maxDiffPos = maxPtsDiff > 0 ? elapsedTime(maxPtsDiffPos) : 0.0;

		sb.append(
			"{ \"totalsrcframes\": %d, \"totaloutframes\": %d, \"totaloutuniqueframes\": %d, "
			"\"notincludedper\": %g, \"avgdiff\": %g, \"maxdiff\": %g, \"maxdiffpos\": %g }",
			totalSrcFrames, totalAudioFrames, totalUniquAudioFrames, 
			(double)notIncluded * 100 / totalSrcFrames, avgDiff, maxDiff, maxDiffPos);
	}

private:
	double elapsedTime(double modPTS) const {
		return (double)(modPTS - basePts) / MPEG_CLOCK_HZ;
	}
};

struct FilterSourceFrame {
	bool halfDelay;
	int frameIndex; // 内部用
	double pts; // 内部用
	double frameDuration; // 内部用
	int64_t framePTS;
	int64_t fileOffset;
	int keyFrame;
	CMType cmType;
};

struct FilterAudioFrame {
	int frameIndex; // デバッグ用
	int64_t waveOffset;
	int waveLength;
};

struct FilterOutVideoInfo {
	int numFrames;
	int frameRateNum;
	int frameRateDenom;
	int fakeAudioSampleRate;
	std::vector<int> fakeAudioSamples;
};

struct OutCaptionLine {
	double start, end;
	CaptionLine* line;
};

typedef std::vector<std::vector<OutCaptionLine>> OutCaptionList;

struct NicoJKLine {
	double start, end;
	std::string line;

	void Write(const File& file) const {
		file.writeValue(start);
		file.writeValue(end);
		file.writeString(line);
	}

	static NicoJKLine Read(const File& file) {
		NicoJKLine item;
		item.start = file.readValue<double>();
		item.end = file.readValue<double>();
		item.line = file.readString();
		return item;
	}
};

typedef std::array<std::vector<NicoJKLine>, NICOJK_MAX> NicoJKList;

typedef std::pair<int64_t, JSTTime> TimeInfo;

class StreamReformInfo : public AMTObject {
public:
	StreamReformInfo(
		AMTContext& ctx,
		int numVideoFile,
		std::vector<FileVideoFrameInfo>& videoFrameList,
		std::vector<FileAudioFrameInfo>& audioFrameList,
		std::vector<CaptionItem>& captionList,
		std::vector<StreamEvent>& streamEventList,
		std::vector<TimeInfo>& timeList)
		: AMTObject(ctx)
		, numVideoFile_(numVideoFile)
		, videoFrameList_(std::move(videoFrameList))
		, audioFrameList_(std::move(audioFrameList))
		, captionItemList_(std::move(captionList))
		, streamEventList_(std::move(streamEventList))
		, timeList_(std::move(timeList))
		, isVFR_(false)
		, hasRFF_(false)
		, srcTotalDuration_()
		, outTotalDuration_()
		, firstFrameTime_()
	{ }

	// 1.
	AudioDiffInfo prepare(bool splitSub) {
		reformMain(splitSub);
		return genWaveAudioStream();
	}

	// 2.
	void applyCMZones(int videoFileIndex, const std::vector<EncoderZone>& cmzones) {
		auto& frames = filterFrameList_[videoFileIndex];
		for (auto zone : cmzones) {
			for (int i = zone.startFrame; i < zone.endFrame; ++i) {
				frames[i].cmType = CMTYPE_CM;
			}
		}
	}

	time_t getFirstFrameTime() const {
		return firstFrameTime_;
	}

	void SetNicoJKList(const std::array<std::vector<NicoJKLine>, NICOJK_MAX>& nicoJKList) {
		for (int t = 0; t < NICOJK_MAX; ++t) {
			nicoJKList_[t].resize(nicoJKList[t].size());
			double startTime = dataPTS_.front();
			for (int i = 0; i < (int)nicoJKList[t].size(); ++i) {
				auto& src = nicoJKList[t][i];
				auto& dst = nicoJKList_[t][i];
				// 開始映像オフセットを加算
				dst.start = src.start + startTime;
				dst.end = src.end + startTime;
				dst.line = src.line;
			}
		}
	}

	// 3.
	AudioDiffInfo genAudio() {
		calcSizeAndTime();
		genCaptionStream();
		return genAudioStream();
	}

	//AudioDiffInfo prepareEncode() {
	//	reformMain();
	//	genAudioStream();
	//	return genWaveAudioStream();
	//}

	// 中間映像ファイルの個数
	int getNumVideoFile() const {
		return numVideoFile_;
	}

	// 入力映像規格
	VIDEO_STREAM_FORMAT getVideoStreamFormat() const {
		return videoFrameList_[0].format.format;
	}

	// PMT変更PTSリスト
	std::vector<int> getPidChangedList(int videoFileIndex) const {
		std::vector<int> ret;
		auto& frames = filterFrameList_[videoFileIndex];
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			if (streamEventList_[i].type == PID_TABLE_CHANGED) {
				FilterSourceFrame tmp = FilterSourceFrame();
				tmp.pts = streamEventPTS_[i];
				auto idx = std::lower_bound(frames.begin(), frames.end(), tmp,
					[&](const FilterSourceFrame& e, const FilterSourceFrame& value) {
					return dataPTS_[e.frameIndex] < value.pts;
				}) - frames.begin();
				if (ret.size() == 0 || ret.back() != idx) {
					ret.push_back((int)idx);
				}
			}
		}
		return ret;
	}

	// フィルタ入力映像フレーム
	const std::vector<FilterSourceFrame>& getFilterSourceFrames(int videoFileIndex) const {
		return filterFrameList_[videoFileIndex];
	}

	// フィルタ入力音声フレーム
	const std::vector<FilterAudioFrame>& getFilterSourceAudioFrames(int videoFileIndex) const {
		return filterAudioFrameList_[videoFileIndex];
	}

	// 中間一時ファイルごとの出力ファイル数
	int getNumEncoders(int videoFileIndex) const {
		return int(
			videoFileStartIndex_[videoFileIndex + 1] - videoFileStartIndex_[videoFileIndex]);
	}

	// 合計出力ファイル数
	int getNumOutFiles() const {
		return (int)fileFormatId_.size();
	}

	// video frame index -> VideoFrameInfo
	const VideoFrameInfo& getVideoFrameInfo(int frameIndex) const {
		return videoFrameList_[frameIndex];
	}

	// video frame index -> encoder index
	int getEncoderIndex(int frameIndex) const {
		int fileId = frameFileId_[frameIndex];
		const auto& format = outFormat_[fileFormatId_[fileId]];
		return fileId - videoFormatStartIndex_[format.videoFileId];
	}

	const OutVideoFormat& getFormat(int encoderIndex, int videoFileIndex) const {
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return outFormat_[fileFormatId_[fileId]];
	}

	// 出力通し番号
	int getOutFileIndex(int encoderIndex, int videoFileIndex) const {
		return videoFileStartIndex_[videoFileIndex] + encoderIndex;
	}

	// 映像データサイズ（バイト）、時間（タイムスタンプ）のペア
	std::pair<int64_t, double> getSrcVideoInfo(int encoderIndex, int videoFileIndex) const {
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return std::make_pair(fileSrcSize_[fileId], fileSrcDuration_[fileId]);
	}

	const FileAudioFrameList& getFileAudioFrameList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedAudioFrameList_[fileId][cmtype].get();
	}

	const OutCaptionList& getOutCaptionList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedCationList_[fileId][cmtype].get();
	}

	const NicoJKList& getOutNicoJKList(
		int encoderIndex, int videoFileIndex, CMType cmtype) const
	{
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return *reformedNicoJKList_[fileId][cmtype].get();
	}

	// TODO: VFR用タイムコード取得
	// infps: フィルタ入力のFPS
	// outpfs: フィルタ出力のFPS
	void getTimeCode(
		int encoderIndex, int videoFileIndex, CMType cmtype, double infps, double outfps) const
	{
		//
	}

	// 各ファイルの再生時間
	double getFileDuration(int encoderIndex, int videoFileIndex, CMType cmtype) const {
		int fileId = videoFileStartIndex_[videoFileIndex] + encoderIndex;
		return fileDuration_[fileId][cmtype];
	}

	const std::vector<int64_t>& getAudioFileOffsets() const {
		return audioFileOffsets_;
	}

	const std::vector<int>& getOutFileMapping() const {
		return outFileIndex_;
	}

	bool isVFR() const {
		return isVFR_;
	}

	bool hasRFF() const {
		return hasRFF_;
	}

	double getInDuration() const {
		return srcTotalDuration_;
	}

	std::pair<double, double> getInOutDuration() const {
		return std::make_pair(srcTotalDuration_, outTotalDuration_);
	}

	void printOutputMapping(std::function<tstring(int)> getFileName) const
	{
		ctx.info("[出力ファイル]");
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			ctx.infoF("%d: %s", i, getFileName(i));
		}

		ctx.info("[入力->出力マッピング]");
		double fromPTS = dataPTS_[0];
		int prevFileId = 0;
		for (int i = 0; i < (int)ordredVideoFrame_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			double pts = modifiedPTS_[ordered];
			int fileId = frameFileId_[ordered];
			if (prevFileId != fileId) {
				// print
				auto from = elapsedTime(fromPTS);
				auto to = elapsedTime(pts);
				ctx.infoF("%3d分%05.3f秒 - %3d分%05.3f秒 -> %d",
					from.first, from.second, to.first, to.second, outFileIndex_[prevFileId]);
				prevFileId = fileId;
				fromPTS = pts;
			}
		}
		auto from = elapsedTime(fromPTS);
		auto to = elapsedTime(dataPTS_.back());
		ctx.infoF("%3d分%05.3f秒 - %3d分%05.3f秒 -> %d",
			from.first, from.second, to.first, to.second, outFileIndex_[prevFileId]);
	}

	// 以下デバッグ用 //

	void serialize(const tstring& path) {
		serialize(File(path, _T("wb")));
	}

	void serialize(const File& file) {
		file.writeValue(numVideoFile_);
		file.writeArray(videoFrameList_);
		file.writeArray(audioFrameList_);
		WriteArray(file, captionItemList_);
		file.writeArray(streamEventList_);
		file.writeArray(timeList_);
	}

	static StreamReformInfo deserialize(AMTContext& ctx, const tstring& path) {
		return deserialize(ctx, File(path, _T("rb")));
	}

	static StreamReformInfo deserialize(AMTContext& ctx, const File& file) {
		int numVideoFile = file.readValue<int>();
		auto videoFrameList = file.readArray<FileVideoFrameInfo>();
		auto audioFrameList = file.readArray<FileAudioFrameInfo>();
		auto captionList = ReadArray<CaptionItem>(file);
		auto streamEventList = file.readArray<StreamEvent>();
		auto timeList = file.readArray<TimeInfo>();
		return StreamReformInfo(ctx,
			numVideoFile, videoFrameList, audioFrameList, captionList, streamEventList, timeList);
	}

private:

	struct CaptionDuration {
		double startPTS, endPTS;
	};

	// 入力解析の出力
	int numVideoFile_;
	std::vector<FileVideoFrameInfo> videoFrameList_; // [DTS順] 
	std::vector<FileAudioFrameInfo> audioFrameList_;
	std::vector<CaptionItem> captionItemList_;
	std::vector<StreamEvent> streamEventList_;
	std::vector<TimeInfo> timeList_;

	std::array<std::vector<NicoJKLine>, NICOJK_MAX> nicoJKList_;

	// 計算データ
	bool isVFR_;
	bool hasRFF_;
	std::vector<double> modifiedPTS_; // [DTS順] ラップアラウンドしないPTS
	std::vector<double> modifiedAudioPTS_; // ラップアラウンドしないPTS
	std::vector<double> modifiedCaptionPTS_; // ラップアラウンドしないPTS
	std::vector<double> audioFrameDuration_; // 各音声フレームの時間
	std::vector<int> ordredVideoFrame_; // [PTS順] -> [DTS順] 変換
	std::vector<double> dataPTS_; // [DTS順] 映像フレームのストリーム上での位置とPTSの関連付け
	std::vector<double> streamEventPTS_;
	std::vector<CaptionDuration> captionDuration_;

	std::vector<std::vector<int>> indexAudioFrameList_; // 音声インデックスごとのフレームリスト

	std::vector<OutVideoFormat> outFormat_;
	// 中間映像ファイルごとのフォーマット開始インデックス
	// サイズは中間映像ファイル数+1
	std::vector<int> videoFormatStartIndex_;

	std::vector<int> fileFormatId_;
	// 中間映像ファイルごとのファイル開始インデックス
	// サイズは中間映像ファイル数+1
	std::vector<int> videoFileStartIndex_;

	// 中間映像ファイルごと
	std::vector<std::vector<FilterSourceFrame>> filterFrameList_; // [PTS順]
	std::vector<std::vector<FilterAudioFrame>> filterAudioFrameList_;

	std::vector<int> frameFileId_; // videoFrameList_と同じサイズ
	//std::map<int64_t, int> framePtsMap_;

	// 出力ファイルごとの入力映像データサイズ、時間
	std::vector<int64_t> fileSrcSize_;
	std::vector<double> fileSrcDuration_;
	std::vector<std::array<double, CMTYPE_MAX>> fileDuration_; // 各ファイルの再生時間

	// 最初の映像フレームの時刻(UNIX時間)
	time_t firstFrameTime_;

	// 2nd phase 出力
	//std::vector<bool> encodedFrames_;

	// 音声構築用
	std::vector<std::array<std::unique_ptr<FileAudioFrameList>, CMTYPE_MAX>> reformedAudioFrameList_;

	std::vector<int64_t> audioFileOffsets_; // 音声ファイルキャッシュ用
	std::vector<int> outFileIndex_; // 出力番号マッピング

	double srcTotalDuration_;
	double outTotalDuration_;

	// 字幕構築用
	std::vector<std::array<std::unique_ptr<OutCaptionList>, CMTYPE_MAX>> reformedCationList_;
	std::vector<std::array<std::unique_ptr<NicoJKList>, CMTYPE_MAX>> reformedNicoJKList_;

	void reformMain(bool splitSub)
	{
		if (videoFrameList_.size() == 0) {
			THROW(FormatException, "映像フレームが1枚もありません");
		}
		if (audioFrameList_.size() == 0) {
			THROW(FormatException, "音声フレームが1枚もありません");
		}
		if (streamEventList_.size() == 0 || streamEventList_[0].type != PID_TABLE_CHANGED) {
			THROW(FormatException, "不正なデータです");
		}

		/*
		// framePtsMap_を作成（すぐに作れるので）
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			framePtsMap_[videoFrameList_[i].PTS] = i;
		}
		*/

		// VFR検出
		isVFR_ = false;
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			if (videoFrameList_[i].format.fixedFrameRate == false) {
				isVFR_ = true;
				break;
			}
		}

		if (isVFR_) {
			THROW(FormatException, "このバージョンはVFRに対応していません");
		}

		// 各コンポーネント開始PTSを映像フレーム基準のラップアラウンドしないPTSに変換
		//（これをやらないと開始フレーム同士が間にラップアラウンドを挟んでると比較できなくなる）
		std::vector<int64_t> startPTSs;
		startPTSs.push_back(videoFrameList_[0].PTS);
		startPTSs.push_back(audioFrameList_[0].PTS);
		if (captionItemList_.size() > 0) {
			startPTSs.push_back(captionItemList_[0].PTS);
		}
		int64_t modifiedStartPTS[3];
		int64_t prevPTS = startPTSs[0];
		for (int i = 0; i < int(startPTSs.size()); ++i) {
			int64_t PTS = startPTSs[i];
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedStartPTS[i] = modPTS;
			prevPTS = modPTS;
		}

		// 各コンポーネントのラップアラウンドしないPTSを生成
		makeModifiedPTS(modifiedStartPTS[0], modifiedPTS_, videoFrameList_);
		makeModifiedPTS(modifiedStartPTS[1], modifiedAudioPTS_, audioFrameList_);
		makeModifiedPTS(modifiedStartPTS[2], modifiedCaptionPTS_, captionItemList_);

		// audioFrameDuration_を生成
		audioFrameDuration_.resize(audioFrameList_.size());
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			const auto& frame = audioFrameList_[i];
			audioFrameDuration_[i] = (frame.numSamples * MPEG_CLOCK_HZ) / (double)frame.format.sampleRate;
		}

		// ptsOrdredVideoFrame_を生成
		ordredVideoFrame_.resize(videoFrameList_.size());
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			ordredVideoFrame_[i] = i;
		}
		std::sort(ordredVideoFrame_.begin(), ordredVideoFrame_.end(), [&](int a, int b) {
			return modifiedPTS_[a] < modifiedPTS_[b];
		});

		// dataPTSを生成
		// 後ろから見てその時点で最も小さいPTSをdataPTSとする
		double curMin = INFINITY;
		double curMax = 0;
		dataPTS_.resize(videoFrameList_.size());
		for (int i = (int)videoFrameList_.size() - 1; i >= 0; --i) {
			curMin = std::min(curMin, modifiedPTS_[i]);
			curMax = std::max(curMax, modifiedPTS_[i]);
			dataPTS_[i] = curMin;
		}

		// 字幕の開始・終了を計算
		captionDuration_.resize(captionItemList_.size());
		double curEnd = dataPTS_.back();
		for (int i = (int)captionItemList_.size() - 1; i >= 0; --i) {
			double modPTS = modifiedCaptionPTS_[i] + (captionItemList_[i].waitTime * (MPEG_CLOCK_HZ / 1000));
			if (captionItemList_[i].line) {
				captionDuration_[i].startPTS = modPTS;
				captionDuration_[i].endPTS = curEnd;
			}
			else {
				// クリア
				captionDuration_[i].startPTS = captionDuration_[i].endPTS = modPTS;
				// 終了を更新
				curEnd = modPTS;
			}
		}

		// ストリームイベントのPTSを計算
		double endPTS = curMax + 1;
		streamEventPTS_.resize(streamEventList_.size());
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			double pts = -1;
			if (ev.type == PID_TABLE_CHANGED || ev.type == VIDEO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)videoFrameList_.size()) {
					// 後ろ過ぎて対象のフレームがない
					pts = endPTS;
				}
				else {
					pts = dataPTS_[ev.frameIdx];
				}
			}
			else if (ev.type == AUDIO_FORMAT_CHANGED) {
				if (ev.frameIdx >= (int)audioFrameList_.size()) {
					// 後ろ過ぎて対象のフレームがない
					pts = endPTS;
				}
				else {
					pts = modifiedAudioPTS_[ev.frameIdx];
				}
			}
			streamEventPTS_[i] = pts;
		}

		// 時間的に近いストリームイベントを1つの変化点とみなす
		const double CHANGE_TORELANCE = 3 * MPEG_CLOCK_HZ;

		std::vector<int> sectionFormatList;
		std::vector<double> startPtsList;

		ctx.info("[フォーマット切り替え解析]");

		// 現在の音声フォーマットを保持
		// 音声ES数が変化しても前の音声フォーマットと変わらない場合は
		// イベントが飛んでこないので、現在の音声ES数とは関係なく全音声フォーマットを保持する
		std::vector<AudioFormat> curAudioFormats;

		OutVideoFormat curFormat = OutVideoFormat();
		double startPts = -1;
		double curFromPTS = -1;
		double curVideoFromPTS = -1;
		curFormat.videoFileId = -1;
		auto addSection = [&]() {
			registerOrGetFormat(curFormat);
			sectionFormatList.push_back(curFormat.formatId);
			startPtsList.push_back(curFromPTS);
			if (startPts == -1) {
				startPts = curFromPTS;
			}
			ctx.infoF("%.2f -> %d", (curFromPTS - startPts) / 90000.0, curFormat.formatId);
			curFromPTS = -1;
			curVideoFromPTS = -1;
		};
		for (int i = 0; i < (int)streamEventList_.size(); ++i) {
			auto& ev = streamEventList_[i];
			double pts = streamEventPTS_[i];
			if (pts >= endPTS) {
				// 後ろに映像がなければ意味がない
				continue;
			}
			if (curFromPTS != -1 &&          // fromがある
				curFormat.videoFileId >= 0 &&  // 映像がある
				curFromPTS + CHANGE_TORELANCE < pts) // CHANGE_TORELANCEより離れている
			{
				// 区間を追加
				addSection();
			}
			// 変更を反映
			switch (ev.type) {
			case PID_TABLE_CHANGED:
				if (curAudioFormats.size() < ev.numAudio) {
					curAudioFormats.resize(ev.numAudio);
				}
				if (curFormat.audioFormat.size() != ev.numAudio) {
					curFormat.audioFormat.resize(ev.numAudio);
					for (int i = 0; i < ev.numAudio; ++i) {
						curFormat.audioFormat[i] = curAudioFormats[i];
					}
					if (curFromPTS == -1) {
						curFromPTS = pts;
					}
				}
				break;
			case VIDEO_FORMAT_CHANGED:
				// ファイル変更
				if (!curFormat.videoFormat.isBasicEquals(videoFrameList_[ev.frameIdx].format)) {
					// アスペクト比以外も変更されていたらファイルを分ける
					//（AMTSplitterと条件を合わせなければならないことに注意）
					++curFormat.videoFileId;
					videoFormatStartIndex_.push_back((int)outFormat_.size());
				}
				curFormat.videoFormat = videoFrameList_[ev.frameIdx].format;
				if (curVideoFromPTS != -1) {
					// 映像フォーマットの変更を区間として取りこぼすと
					// AMTSplitterとの整合性が取れなくなるので強制的に追加
					addSection();
				}
				// 映像フォーマットの変更時刻を優先させる
				curFromPTS = curVideoFromPTS = dataPTS_[ev.frameIdx];
				break;
			case AUDIO_FORMAT_CHANGED:
				if (ev.audioIdx >= (int)curFormat.audioFormat.size()) {
					THROW(FormatException, "StreamEvent's audioIdx exceeds numAudio of the previous table change event");
				}
				curFormat.audioFormat[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				curAudioFormats[ev.audioIdx] = audioFrameList_[ev.frameIdx].format;
				if (curFromPTS == -1) {
					curFromPTS = pts;
				}
				break;
			}
		}
		// 最後の区間を追加
		if (curFromPTS != -1) {
			addSection();
		}
		startPtsList.push_back(endPTS);
		videoFormatStartIndex_.push_back((int)outFormat_.size());

		// frameSectionIdを生成
		std::vector<int> outFormatFrames(outFormat_.size());
		std::vector<int> frameSectionId(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			double pts = modifiedPTS_[i];
			// 区間を探す
			int sectionId = int(std::partition_point(startPtsList.begin(), startPtsList.end(),
				[=](double sec) {
				return !(pts < sec);
			}) - startPtsList.begin() - 1);
			if (sectionId >= (int)sectionFormatList.size()) {
				THROWF(RuntimeException, "sectionId exceeds section count (%d >= %d) at frame %d",
					sectionId, (int)sectionFormatList.size(), i);
			}
			frameSectionId[i] = sectionId;
			outFormatFrames[sectionFormatList[sectionId]]++;
		}

		// セクション→ファイルマッピングを生成
		std::vector<int> sectionFileList(sectionFormatList.size());

		if (splitSub) {
			// メインフォーマット以外は結合しない //

			int mainFormatId = int(std::max_element(
				outFormatFrames.begin(), outFormatFrames.end()) - outFormatFrames.begin());

			videoFileStartIndex_.push_back(0);
			for (int i = 0, mainFileId = -1, nextFileId = 0, videoId = 0;
				i < (int)sectionFormatList.size(); ++i)
			{
				int vid = outFormat_[sectionFormatList[i]].videoFileId;
				if (videoId != vid) {
					videoFileStartIndex_.push_back(nextFileId);
					videoId = vid;
				}
				if (sectionFormatList[i] == mainFormatId) {
					if (mainFileId == -1) {
						mainFileId = nextFileId++;
						fileFormatId_.push_back(mainFormatId);
					}
					sectionFileList[i] = mainFileId;
				}
				else {
					sectionFileList[i] = nextFileId++;
					fileFormatId_.push_back(sectionFormatList[i]);
				}
			}
			videoFileStartIndex_.push_back((int)fileFormatId_.size());
		}
		else {
			for (int i = 0; i < (int)sectionFormatList.size(); ++i) {
				// ファイルとフォーマットは同じ
				sectionFileList[i] = sectionFormatList[i];
			}
			for (int i = 0; i < (int)outFormat_.size(); ++i) {
				// ファイルとフォーマットは恒等変換
				fileFormatId_.push_back(i);
			}
			videoFileStartIndex_ = videoFormatStartIndex_;
		}

		// frameFileId_を生成
		frameFileId_.resize(videoFrameList_.size());
		for (int i = 0; i < int(videoFrameList_.size()); ++i) {
			frameFileId_[i] = sectionFileList[frameSectionId[i]];
		}

		// フィルタ用入力フレームリスト生成
		filterFrameList_ = std::vector<std::vector<FilterSourceFrame>>(numVideoFile_);
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			int keyFrame = -1;
			std::vector<FilterSourceFrame>& list = filterFrameList_[videoId];

			const auto& format = outFormat_[videoFormatStartIndex_[videoId]].videoFormat;
			double timePerFrame = format.frameRateDenom * MPEG_CLOCK_HZ / (double)format.frameRateNum;

			for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
				int ordered = ordredVideoFrame_[i];
				int formatId = fileFormatId_[frameFileId_[ordered]];
				if (outFormat_[formatId].videoFileId == videoId) {

					double mPTS = modifiedPTS_[ordered];
					FileVideoFrameInfo& srcframe = videoFrameList_[ordered];
					if (srcframe.isGopStart) {
						keyFrame = int(list.size());
					}

					// まだキーフレームがない場合は捨てる
					if (keyFrame == -1) continue;

					FilterSourceFrame frame;
					frame.halfDelay = false;
					frame.frameIndex = i;
					frame.pts = mPTS;
					frame.frameDuration = timePerFrame; // TODO: VFR対応
					frame.framePTS = (int64_t)mPTS;
					frame.fileOffset = srcframe.fileOffset;
					frame.keyFrame = keyFrame;
					frame.cmType = CMTYPE_NONCM; // 最初は全部NonCMにしておく

					switch (srcframe.pic) {
					case PIC_FRAME:
					case PIC_TFF:
					case PIC_TFF_RFF:
						list.push_back(frame);
						break;
					case PIC_FRAME_DOUBLING:
						list.push_back(frame);
						frame.pts += timePerFrame;
						list.push_back(frame);
						break;
					case PIC_FRAME_TRIPLING:
						list.push_back(frame);
						frame.pts += timePerFrame;
						list.push_back(frame);
						frame.pts += timePerFrame;
						list.push_back(frame);
						break;
					case PIC_BFF:
						frame.halfDelay = true;
						frame.pts -= timePerFrame / 2;
						list.push_back(frame);
						break;
					case PIC_BFF_RFF:
						frame.halfDelay = true;
						frame.pts -= timePerFrame / 2;
						list.push_back(frame);
						frame.halfDelay = false;
						frame.pts += timePerFrame;
						list.push_back(frame);
						break;
					}
				}
			}
		}

		// indexAudioFrameList_を作成
		int numMaxAudio = 1;
		for (int i = 0; i < (int)outFormat_.size(); ++i) {
			numMaxAudio = std::max(numMaxAudio, (int)outFormat_[i].audioFormat.size());
		}
		indexAudioFrameList_.resize(numMaxAudio);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
      // 短すぎてセクションとして認識されなかった部分に
      // numMaxAudioを超える音声データが存在する可能性がある
      // 音声数を超えている音声フレームは無視する
      if (audioFrameList_[i].audioIdx < numMaxAudio) {
        indexAudioFrameList_[audioFrameList_[i].audioIdx].push_back(i);
      }
		}

		// audioFileOffsets_を生成
		audioFileOffsets_.resize(audioFrameList_.size() + 1);
		for (int i = 0; i < (int)audioFrameList_.size(); ++i) {
			audioFileOffsets_[i] = audioFrameList_[i].fileOffset;
		}
		const auto& lastFrame = audioFrameList_.back();
		audioFileOffsets_.back() = lastFrame.fileOffset + lastFrame.codedDataSize;

		// 時間情報
		srcTotalDuration_ = dataPTS_.back() - dataPTS_.front();
		if (timeList_.size() > 0) {
			auto ti = timeList_[0];
			// ラップアラウンドしてる可能性があるので上位ビットは捨てて計算
			double diff = (double)(int32_t(ti.first / 300 - dataPTS_.front())) / MPEG_CLOCK_HZ;
			tm t = tm();
			ti.second.getDay(t.tm_year, t.tm_mon, t.tm_mday);
			ti.second.getTime(t.tm_hour, t.tm_min, t.tm_sec);
			// 調整
			t.tm_mon -= 1; // 月は0始まりなので
			t.tm_year -= 1900; // 年は1900を引く
			t.tm_hour -= 9; // 日本なのでGMT+9
			t.tm_sec -= (int)std::round(diff); // 最初のフレームまで戻す
			firstFrameTime_ = _mkgmtime(&t);
		}
	}

	void calcSizeAndTime()
	{
		// 各ファイルの入力ファイル時間とサイズを計算
		// ソースフレームから
		fileSrcSize_ = std::vector<int64_t>(fileFormatId_.size(), 0);
		fileSrcDuration_ = std::vector<double>(fileFormatId_.size(), 0);
		for (int i = 0; i < (int)videoFrameList_.size(); ++i) {
			int ordered = ordredVideoFrame_[i];
			int fileId = frameFileId_[ordered];
			int next = (i + 1 < (int)videoFrameList_.size())
				? ordredVideoFrame_[i + 1]
				: -1;
			double duration = getSourceFrameDuration(ordered, next);

			const auto& frame = videoFrameList_[ordered];
			fileSrcSize_[fileId] += frame.codedDataSize;
			fileSrcDuration_[fileId] += duration;
		}

		// 各ファイルの出力ファイル時間を計算
		// CM判定はフィルタ入力フレームに適用されているのでフィルタ入力フレームから
		fileDuration_ = std::vector<std::array<double, CMTYPE_MAX>>(fileFormatId_.size(), std::array<double, CMTYPE_MAX>());
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			std::vector<FilterSourceFrame>& list = filterFrameList_[videoId];
			for (int i = 0; i < (int)list.size(); ++i) {
				int fileId = frameFileId_[list[i].frameIndex];
				double duration = list[i].frameDuration;
				fileDuration_[fileId][CMTYPE_BOTH] += duration;
				fileDuration_[fileId][list[i].cmType] += duration;
			}
		}

		// 統計
		double sumDuration = 0;
		double maxDuration = 0;
		int maxId = 0;
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			double time = fileDuration_[i][CMTYPE_BOTH];
			sumDuration += time;
			if (maxDuration < time) {
				maxDuration = time;
				maxId = i;
			}
		}
		outTotalDuration_ = sumDuration;

		// 出力ファイル番号生成
		outFileIndex_.resize(fileFormatId_.size());
		outFileIndex_[maxId] = 0;
		for (int i = 0, cnt = 1; i < (int)fileFormatId_.size(); ++i) {
			if (i != maxId) {
				outFileIndex_[i] = cnt++;
			}
		}
	}

	template<typename I>
	void makeModifiedPTS(int64_t modifiedFirstPTS, std::vector<double>& modifiedPTS, const std::vector<I>& frames)
	{
		// 前後のフレームのPTSに6時間以上のずれがあると正しく処理できない
		if (frames.size() == 0) return;

		// ラップアラウンドしないPTSを生成
		modifiedPTS.resize(frames.size());
		int64_t prevPTS = modifiedFirstPTS;
		for (int i = 0; i < int(frames.size()); ++i) {
			int64_t PTS = frames[i].PTS;
			if (PTS == -1) {
				// PTSがない
				THROWF(FormatException,
					"PTSがありません。処理できません。 %dフレーム目", i);
			}
			int64_t modPTS = prevPTS + int64_t((int32_t(PTS) - int32_t(prevPTS)));
			modifiedPTS[i] = (double)modPTS;
			prevPTS = modPTS;
		}

		// ストリームが戻っている場合は処理できないのでエラーとする
		for (int i = 1; i < int(frames.size()); ++i) {
			if (modifiedPTS[i] - modifiedPTS[i - 1] < -60 * MPEG_CLOCK_HZ) {
				// 1分以上戻っている
				ctx.incrementCounter(AMT_ERR_NON_CONTINUOUS_PTS);
				ctx.warnF("PTSが戻っています。正しく処理できないかもしれません。 [%d] %.0f -> %.0f",
					i, modifiedPTS[i - 1], modifiedPTS[i]);
			}
		}
	}

	void registerOrGetFormat(OutVideoFormat& format) {
		// すでにあるのから探す
		for (int i = videoFormatStartIndex_.back(); i < (int)outFormat_.size(); ++i) {
			if (isEquealFormat(outFormat_[i], format)) {
				format.formatId = i;
				return;
			}
		}
		// ないので登録
		format.formatId = (int)outFormat_.size();
		outFormat_.push_back(format);
	}

	bool isEquealFormat(const OutVideoFormat& a, const OutVideoFormat& b) {
		if (a.videoFormat != b.videoFormat) return false;
		if (a.audioFormat.size() != b.audioFormat.size()) return false;
		for (int i = 0; i < (int)a.audioFormat.size(); ++i) {
			if (a.audioFormat[i] != b.audioFormat[i]) {
				return false;
			}
		}
		return true;
	}

	struct AudioState {
		double time = 0; // 追加された音声フレームの合計時間
		double lostPts = -1; // 同期ポイントを見失ったPTS（表示用）
		int lastFrame = -1;
	};

	struct OutFileState {
		int formatId; // デバッグ出力用
		double time; // 追加された映像フレームの合計時間
		std::vector<AudioState> audioState;
		std::unique_ptr<FileAudioFrameList> audioFrameList;
	};

	AudioDiffInfo initAudioDiffInfo() {
		AudioDiffInfo adiff = AudioDiffInfo();
		adiff.totalSrcFrames = (int)audioFrameList_.size();
		adiff.basePts = dataPTS_[0];
		return adiff;
	}

	// フィルタ入力から音声構築
	AudioDiffInfo genAudioStream()
	{
		AudioDiffInfo adiff = initAudioDiffInfo();

		std::vector<std::array<OutFileState, CMTYPE_MAX>> outFiles(fileFormatId_.size());

		// outFiles初期化
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				auto& file = outFiles[i][c];
				int numAudio = (int)outFormat_[fileFormatId_[i]].audioFormat.size();
				file.formatId = i;
				file.time = 0;
				file.audioState.resize(numAudio);
				file.audioFrameList =
					std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(numAudio));
			}
		}

		// 全映像フレームを追加
		ctx.info("[音声構築]");
		for (int videoId = 0; videoId < numVideoFile_; ++videoId) {
			auto& frames = filterFrameList_[videoId];
			for (int i = 0; i < (int)frames.size(); ++i) {
				int ordered = ordredVideoFrame_[frames[i].frameIndex];
				int fileId = frameFileId_[ordered];
				addVideoFrame(outFiles[fileId][frames[i].cmType], fileId, frames[i].pts, frames[i].frameDuration, nullptr);
				addVideoFrame(outFiles[fileId][CMTYPE_BOTH], fileId, frames[i].pts, frames[i].frameDuration, &adiff);
			}
		}

		// 出力データ生成
		reformedAudioFrameList_.resize(fileFormatId_.size());
		for (int i = 0; i < (int)fileFormatId_.size(); ++i) {
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				double time = outFiles[i][c].time;
				reformedAudioFrameList_[i][c] = std::move(outFiles[i][c].audioFrameList);
			}
		}

		return adiff;
	}

	AudioDiffInfo genWaveAudioStream()
	{
		AudioDiffInfo adiff = initAudioDiffInfo();

		// 全映像フレームを追加
		ctx.info("[CM判定用音声構築]");
		filterAudioFrameList_.resize(numVideoFile_);
		for (int videoId = 0; videoId < (int)numVideoFile_; ++videoId) {
			OutFileState file = { 0 };
			file.formatId = -1;
			file.time = 0;
			file.audioState.resize(1);
			file.audioFrameList =
				std::unique_ptr<FileAudioFrameList>(new FileAudioFrameList(1));

			auto& frames = filterFrameList_[videoId];
			auto& format = outFormat_[videoFormatStartIndex_[videoId]];
			double timePerFrame = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / (double)format.videoFormat.frameRateNum;

			for (int i = 0; i < (int)frames.size(); ++i) {
				double endPts = frames[i].pts + timePerFrame;
				file.time += timePerFrame;

				// file.timeまで音声を進める
				auto& audioState = file.audioState[0];
				if (audioState.time < file.time) {
					double audioDuration = file.time - audioState.time;
					double audioPts = endPts - audioDuration;
					// ステレオに変換されているはずなので、音声フォーマットは問わない
					fillAudioFrames(file, 0, nullptr, audioPts, audioDuration, &adiff);
				}
			}

			auto& list = file.audioFrameList->at(0);
			for (int i = 0; i < (int)list.size(); ++i) {
				FilterAudioFrame frame = { 0 };
				auto& info = audioFrameList_[list[i]];
				frame.frameIndex = list[i];
				frame.waveOffset = info.waveOffset;
				frame.waveLength = info.waveDataSize;
				filterAudioFrameList_[videoId].push_back(frame);
			}
		}

		return adiff;
	}

	// フレームの表示時間
	// RFF: true=ソースフレームの表示時間 false=フィルタ入力フレーム（RFF処理済み）の表示時間
	template <bool RFF>
	double getFrameDuration(int index, int nextIndex)
	{
		const auto& videoFrame = videoFrameList_[index];
		int formatId = fileFormatId_[frameFileId_[index]];
		const auto& format = outFormat_[formatId];
		double frameDiff = format.videoFormat.frameRateDenom * MPEG_CLOCK_HZ / (double)format.videoFormat.frameRateNum;

		double duration;
		if (isVFR_) { // VFR
			if (nextIndex == -1) {
				duration = 0; // 最後のフレーム
			}
			else {
				duration = modifiedPTS_[nextIndex] - modifiedPTS_[index];
			}
		}
		else { // CFR
			if (RFF) {
				switch (videoFrame.pic) {
				case PIC_FRAME:
				case PIC_TFF:
					duration = frameDiff;
					break;
				case PIC_TFF_RFF:
					duration = frameDiff * 1.5;
					break;
				case PIC_FRAME_DOUBLING:
					duration = frameDiff * 2;
					hasRFF_ = true;
					break;
				case PIC_FRAME_TRIPLING:
					duration = frameDiff * 3;
					hasRFF_ = true;
					break;
				case PIC_BFF:
					duration = frameDiff;
					break;
				case PIC_BFF_RFF:
					duration = frameDiff * 1.5;
					hasRFF_ = true;
					break;
				}
			}
			else {
				duration = frameDiff;
			}
		}

		return duration;
	}

	// ソースフレームの表示時間
	double getSourceFrameDuration(int index, int nextIndex) {
		return getFrameDuration<true>(index, nextIndex);
	}

	// フィルタ入力フレーム（RFF処理済み）の表示時間
	double getFilterSourceFrameDuration(int index, int nextIndex) {
		return getFrameDuration<false>(index, nextIndex);
	}

	void addVideoFrame(OutFileState& file, int fileId, double pts, double duration, AudioDiffInfo* adiff) {
		const auto& format = outFormat_[fileFormatId_[fileId]];
		double endPts = pts + duration;
		file.time += duration;

		ASSERT(format.audioFormat.size() == file.audioFrameList->size());
		ASSERT(format.audioFormat.size() == file.audioState.size());
		for (int i = 0; i < (int)format.audioFormat.size(); ++i) {
			// file.timeまで音声を進める
			auto& audioState = file.audioState[i];
			if (audioState.time >= file.time) {
				// 音声は十分進んでる
				continue;
			}
			double audioDuration = file.time - audioState.time;
			double audioPts = endPts - audioDuration;
			fillAudioFrames(file, i, &format.audioFormat[i], audioPts, audioDuration, adiff);
		}
	}

	void fillAudioFrames(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat* format, // 音声フォーマット
		double pts, double duration, // 開始修正PTSと90kHzでのタイムスパン
		AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];

		fillAudioFramesInOrder(file, index, format, pts, duration, adiff);
		if (duration <= 0) {
			// 十分出力した
			return;
		}

		// もしかしたら戻ったらあるかもしれないので探しなおす
		auto it = std::partition_point(frameList.begin(), frameList.end(), [&](int frameIndex) {
			double modPTS = modifiedAudioPTS_[frameIndex];
			double frameDuration = audioFrameDuration_[frameIndex];
			return modPTS + (frameDuration / 2) < pts;
		});
		if (it != frameList.end()) {
			// 見つけたところに位置をセットして入れてみる
			if (state.lostPts != pts) {
				state.lostPts = pts;
				if (adiff) {
					auto elapsed = elapsedTime(pts);
					ctx.debugF("%d分%.3f秒で音声%d-%dの同期ポイントを見失ったので再検索",
						elapsed.first, elapsed.second, file.formatId, index);
				}
			}
			state.lastFrame = (int)(it - frameList.begin() - 1);
			fillAudioFramesInOrder(file, index, format, pts, duration, adiff);
		}

		// 有効な音声フレームが見つからなかった場合はとりあえず何もしない
		// 次に有効な音声フレームが見つかったらその間はフレーム水増しされる
		// 映像より音声が短くなる可能性はあるが、有効な音声がないのであれば仕方ないし
		// 音ズレするわけではないので問題ないと思われる

	}

	// lastFrameから順番に見て音声フレームを入れる
	void fillAudioFramesInOrder(
		OutFileState& file, int index, // 対象ファイルと音声インデックス
		const AudioFormat* format, // 音声フォーマット
		double& pts, double& duration, // 開始修正PTSと90kHzでのタイムスパン
		AudioDiffInfo* adiff)
	{
		auto& state = file.audioState[index];
		auto& outFrameList = file.audioFrameList->at(index);
		const auto& frameList = indexAudioFrameList_[index];
		int nskipped = 0;

		for (int i = state.lastFrame + 1; i < (int)frameList.size(); ++i) {
			int frameIndex = frameList[i];
			const auto& frame = audioFrameList_[frameIndex];
			double modPTS = modifiedAudioPTS_[frameIndex];
			double frameDuration = audioFrameDuration_[frameIndex];
			double halfDuration = frameDuration / 2;
			double quaterDuration = frameDuration / 4;

			if (modPTS >= pts + duration) {
				// 開始が終了より後ろの場合
				if (modPTS >= pts + frameDuration - quaterDuration) {
					// フレームの4分の3以上のズレている場合
					// 行き過ぎ
					break;
				}
			}
			if (modPTS + (frameDuration / 2) < pts) {
				// 前すぎるのでスキップ
				++nskipped;
				continue;
			}
			if (format != nullptr && frame.format != *format) {
				// フォーマットが違うのでスキップ
				continue;
			}

			// 空きがある場合はフレームを水増しする
			// フレームの4分の3以上の空きができる場合は埋める
			int nframes = (int)std::max(1.0, ((modPTS - pts) + (frameDuration / 4)) / frameDuration);

			if (adiff) {
				if (nframes > 1) {
					auto elapsed = elapsedTime(modPTS);
					ctx.debugF("%d分%.3f秒で音声%d-%dにずれがあるので%dフレーム水増し",
						elapsed.first, elapsed.second, file.formatId, index, nframes - 1);
				}
				if (nskipped > 0) {
					if (state.lastFrame == -1) {
						ctx.debugF("音声%d-%dは%dフレーム目から開始",
							file.formatId, index, nskipped);
					}
					else {
						auto elapsed = elapsedTime(modPTS);
						ctx.debugF("%d分%.3f秒で音声%d-%dにずれがあるので%dフレームスキップ",
							elapsed.first, elapsed.second, file.formatId, index, nskipped);
					}
					nskipped = 0;
				}

				++adiff->totalUniquAudioFrames;
			}

			for (int t = 0; t < nframes; ++t) {
				// 統計情報
				if (adiff) {
					double diff = std::abs(modPTS - pts);
					if (adiff->maxPtsDiff < diff) {
						adiff->maxPtsDiff = diff;
						adiff->maxPtsDiffPos = pts;
					}
					adiff->sumPtsDiff += diff;
					++adiff->totalAudioFrames;
				}

				// フレームを出力
				outFrameList.push_back(frameIndex);
				state.time += frameDuration;
				pts += frameDuration;
				duration -= frameDuration;
			}

			state.lastFrame = i;
			if (duration <= 0) {
				// 十分出力した
				return;
			}
		}
	}

	// ファイル全体での時間
	std::pair<int, double> elapsedTime(double modPTS) const {
		double sec = (double)(modPTS - dataPTS_[0]) / MPEG_CLOCK_HZ;
		int minutes = (int)(sec / 60);
		sec -= minutes * 60;
		return std::make_pair(minutes, sec);
	}

	void genCaptionStream()
	{
		ctx.info("[字幕構築]");
		reformedCationList_.clear();
		reformedNicoJKList_.clear();

		for (int fileId = 0; fileId < (int)fileFormatId_.size(); ++fileId) {
			int videoId = outFormat_[fileFormatId_[fileId]].videoFileId;
			auto& frames = filterFrameList_[videoId];

			// 各フレームの時刻を生成
			std::array<std::vector<double>, CMTYPE_MAX> frameTimes;
			std::array<double, CMTYPE_MAX> curTimes = { 0 };
			for (int i = 0; i < (int)frames.size(); ++i) {
				for (int c = 0; c < CMTYPE_MAX; ++c) {
					frameTimes[c].push_back(curTimes[c]);
				}
				if (frameFileId_[frames[i].frameIndex] == fileId) {
					curTimes[0] += frames[i].frameDuration;
					curTimes[frames[i].cmType] += frames[i].frameDuration;
				}
			}
			// 最終フレームの終了時刻も追加
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				frameTimes[c].push_back(curTimes[c]);
			}

			// 出力字幕を生成
			reformedCationList_.emplace_back();
			auto& clistItem = reformedCationList_.back();
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				clistItem[c] = std::unique_ptr<OutCaptionList>(new OutCaptionList());
			}
			for (int i = 0; i < (int)captionItemList_.size(); ++i) {
				if (captionItemList_[i].line) { // クリア以外
					auto duration = captionDuration_[i];
					auto start = std::lower_bound(frames.begin(), frames.end(), duration.startPTS,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					auto end = std::lower_bound(frames.begin(), frames.end(), duration.endPTS,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					if (start < end) { // 1フレーム以上表示時間のある場合のみ
						int langIndex = captionItemList_[i].langIndex;
						for (int c = 0; c < CMTYPE_MAX; ++c) {
							double startTime = frameTimes[c][start];
							double endTime = frameTimes[c][end];
							if (startTime < endTime) { // 表示時間のある場合のみ
								if (langIndex >= clistItem[c]->size()) { // 言語が足りない場合は広げる
									clistItem[c]->resize(langIndex + 1);
								}
								OutCaptionLine outcap = { startTime, endTime, captionItemList_[i].line.get() };
								clistItem[c]->at(langIndex).push_back(outcap);
							}
						}
					}
				}
			}

			// ニコニコ実況コメントを生成
			reformedNicoJKList_.emplace_back();
			auto& nlistItem = reformedNicoJKList_.back();
			for (int c = 0; c < CMTYPE_MAX; ++c) {
				nlistItem[c] = std::unique_ptr<NicoJKList>(new NicoJKList());
			}
			double tick = MPEG_CLOCK_HZ / 10;
			for (int t = 0; t < NICOJK_MAX; ++t) {
				auto& srcList = nicoJKList_[t];
				for (int i = 0; i < (int)srcList.size(); ++i) {
					auto item = srcList[i];
					auto start = std::lower_bound(frames.begin(), frames.end(), item.start,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					auto end = std::lower_bound(frames.begin(), frames.end(), item.end,
						[](const FilterSourceFrame& frame, double mid) { return frame.pts < mid; }) - frames.begin();
					// 開始がこのファイルに含まれているか
					if (start < end && frameFileId_[frames[start].frameIndex] == fileId) {
						for (int c = 0; c < CMTYPE_MAX; ++c) {
							// 開始がこのCMタイプに含まれているか
							if (c == 0 || frames[start].cmType == c) {
								double startTime = frameTimes[c][start];
								double endTime = frameTimes[c][end];
								NicoJKLine outcomment = { startTime, endTime, item.line };
								nlistItem[c]->at(t).push_back(outcomment);
							}
						}
					}
				}
			}
		}
	}
};

