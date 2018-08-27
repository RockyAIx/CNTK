//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//

#define _CRT_SECURE_NO_WARNINGS
#include "LTNoRandomizer.h"
#include <mutex>

namespace CNTK {

// Properties used in the checkpoint.
const static std::wstring s_currentChunkPositionProperty = L"currentChunkPosition";
const static std::wstring s_currentSequencePositionProperty = L"currentSequencePosition";

LTNoRandomizer::LTNoRandomizer(DataDeserializerPtr deserializer, bool multithreadedGetNextSequences, size_t maxNumberOfInvalidSequences)
    : Base(deserializer, { { s_currentChunkPositionProperty, 0 }, { s_currentSequencePositionProperty, 0} }, multithreadedGetNextSequences, maxNumberOfInvalidSequences),
      m_currentChunkPosition(0),
      m_currentSequencePosition(0)
{
}

LTNoRandomizer::~LTNoRandomizer()
{
    StopPrefetch();
}

void LTNoRandomizer::Prefetch() const
{
    std::lock_guard<std::mutex> lock(m_prefetchStateMutex);
    auto chunkId = m_originalChunkDescriptions[m_currentChunkPosition].m_id;
    m_prefetchState.m_prefetchedChunk.m_info = m_originalChunkDescriptions[m_currentChunkPosition];
    m_prefetchState.m_prefetchedChunk.m_data = m_deserializer->GetChunk(chunkId);
    m_prefetchState.m_prefetchedChunk.m_sequenceInfos.clear();
    m_prefetchState.m_prefetchedChunk.m_data->SequenceInfos(m_prefetchState.m_prefetchedChunk.m_sequenceInfos);
    //m_originalChunkDescriptions[m_currentChunkPosition] might not have the counts of sequencs and samples
    //especiallly for the deserializers without indexer, such as UserDeserializer
    if (!m_prefetchState.m_prefetchedChunk.m_info.HasCountsInitiated())
        UpdateChunkInfo(m_prefetchState.m_prefetchedChunk.m_info, m_prefetchState.m_prefetchedChunk.m_sequenceInfos);
}

void LTNoRandomizer::RefillSequenceWindow(SequenceWindow& window)
{
    {
        std::lock_guard<std::mutex> lock(m_prefetchStateMutex);
        window.m_sequences.assign(m_prefetchState.m_prefetchedChunk.m_sequenceInfos.begin(), m_prefetchState.m_prefetchedChunk.m_sequenceInfos.end());
        window.m_dataChunks.clear();
        window.m_dataChunks[m_prefetchState.m_prefetchedChunk.m_info.m_id] = m_prefetchState.m_prefetchedChunk.m_data;
    }
    auto numberOfWorkers = Config().m_numberOfWorkers;
    if (numberOfWorkers > 1)
    {
        // Decimate according to the position.
        size_t workerSequencePosition = 0;
        for (size_t i = 0; i < window.m_sequences.size(); ++i, ++m_currentSequencePosition)
        {
            if (m_currentSequencePosition % numberOfWorkers == Config().m_workerRank && workerSequencePosition != i)
                std::swap(window.m_sequences[workerSequencePosition], window.m_sequences[i]);
            ++workerSequencePosition;
        }

        window.m_sequences.erase(window.m_sequences.begin() + workerSequencePosition, window.m_sequences.end());
    }

    // If last chunk, add the sweep marker.
    if (m_currentChunkPosition == m_originalChunkDescriptions.size() - 1)
    {
        window.m_sequences.push_back(s_endOfSweep);
        m_currentSequencePosition = 0;
    }

    // Moving to the next chunk.
    m_currentChunkPosition = (m_currentChunkPosition + 1) % m_originalChunkDescriptions.size();
}

std::map<std::wstring, size_t> LTNoRandomizer::GetInnerState()
{
    std::map<std::wstring, size_t> state;
    state[s_currentChunkPositionProperty] = m_currentChunkPosition;
    state[s_currentSequencePositionProperty] = m_currentSequencePosition;
    return state;
}

void LTNoRandomizer::SetInnerState(const std::map<std::wstring, size_t>& state)
{
    m_currentChunkPosition = (ChunkIdType)ValueFrom(state, s_currentChunkPositionProperty);
    m_currentSequencePosition = ValueFrom(state, s_currentSequencePositionProperty);
}

}
