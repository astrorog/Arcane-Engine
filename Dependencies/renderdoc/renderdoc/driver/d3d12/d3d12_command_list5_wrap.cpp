/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "d3d12_command_list.h"

template <typename SerialiserType>
bool WrappedID3D12GraphicsCommandList::Serialise_RSSetShadingRate(
    SerialiserType &ser, D3D12_SHADING_RATE baseShadingRate,
    const D3D12_SHADING_RATE_COMBINER *combiners)
{
  ID3D12GraphicsCommandList5 *pCommandList = this;
  SERIALISE_ELEMENT(pCommandList);
  SERIALISE_ELEMENT(baseShadingRate);
  SERIALISE_ELEMENT_ARRAY(combiners, 2);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(GetWrapped(pCommandList)->GetReal5() == NULL)
    {
      RDCERR("Can't replay ID3D12GraphicsCommandList5 command");
      return false;
    }

    m_Cmd->m_LastCmdListID = GetResourceManager()->GetOriginalID(GetResID(pCommandList));

    if(IsActiveReplaying(m_State))
    {
      if(m_Cmd->InRerecordRange(m_Cmd->m_LastCmdListID))
      {
        Unwrap5(m_Cmd->RerecordCmdList(m_Cmd->m_LastCmdListID))
            ->RSSetShadingRate(baseShadingRate, combiners);

        if(m_Cmd->IsPartialCmdList(m_Cmd->m_LastCmdListID))
        {
          m_Cmd->m_RenderState.shadingRate = baseShadingRate;
          if(combiners)
          {
            m_Cmd->m_RenderState.shadingRateCombiners[0] = combiners[0];
            m_Cmd->m_RenderState.shadingRateCombiners[1] = combiners[1];
          }
        }
      }
    }
    else
    {
      Unwrap5(pCommandList)->RSSetShadingRate(baseShadingRate, combiners);
      GetCrackedList5()->RSSetShadingRate(baseShadingRate, combiners);

      D3D12RenderState &state = m_Cmd->m_BakedCmdListInfo[m_Cmd->m_LastCmdListID].state;

      state.shadingRate = baseShadingRate;
      if(combiners)
      {
        state.shadingRateCombiners[0] = combiners[0];
        state.shadingRateCombiners[1] = combiners[1];
      }
    }
  }

  return true;
}

void STDMETHODCALLTYPE WrappedID3D12GraphicsCommandList::RSSetShadingRate(
    D3D12_SHADING_RATE baseShadingRate, const D3D12_SHADING_RATE_COMBINER *combiners)
{
  SERIALISE_TIME_CALL(m_pList5->RSSetShadingRate(baseShadingRate, combiners));

  if(IsCaptureMode(m_State))
  {
    CACHE_THREAD_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D12Chunk::List_RSSetShadingRate);
    Serialise_RSSetShadingRate(ser, baseShadingRate, combiners);

    m_ListRecord->AddChunk(scope.Get());
  }
}

template <typename SerialiserType>
bool WrappedID3D12GraphicsCommandList::Serialise_RSSetShadingRateImage(SerialiserType &ser,
                                                                       ID3D12Resource *shadingRateImage)
{
  ID3D12GraphicsCommandList5 *pCommandList = this;
  SERIALISE_ELEMENT(pCommandList);
  SERIALISE_ELEMENT(shadingRateImage);

  SERIALISE_CHECK_READ_ERRORS();

  if(IsReplayingAndReading())
  {
    if(GetWrapped(pCommandList)->GetReal5() == NULL)
    {
      RDCERR("Can't replay ID3D12GraphicsCommandList5 command");
      return false;
    }

    m_Cmd->m_LastCmdListID = GetResourceManager()->GetOriginalID(GetResID(pCommandList));

    if(IsActiveReplaying(m_State))
    {
      if(m_Cmd->InRerecordRange(m_Cmd->m_LastCmdListID))
      {
        Unwrap5(m_Cmd->RerecordCmdList(m_Cmd->m_LastCmdListID))
            ->RSSetShadingRateImage(Unwrap(shadingRateImage));

        if(m_Cmd->IsPartialCmdList(m_Cmd->m_LastCmdListID))
        {
          m_Cmd->m_RenderState.shadingRateImage = GetResID(shadingRateImage);
        }
      }
    }
    else
    {
      Unwrap5(pCommandList)->RSSetShadingRateImage(Unwrap(shadingRateImage));
      GetCrackedList5()->RSSetShadingRateImage(Unwrap(shadingRateImage));

      D3D12RenderState &state = m_Cmd->m_BakedCmdListInfo[m_Cmd->m_LastCmdListID].state;

      state.shadingRateImage = GetResID(shadingRateImage);
    }
  }

  return true;
}

void STDMETHODCALLTYPE
WrappedID3D12GraphicsCommandList::RSSetShadingRateImage(ID3D12Resource *shadingRateImage)
{
  SERIALISE_TIME_CALL(m_pList5->RSSetShadingRateImage(shadingRateImage));

  if(IsCaptureMode(m_State))
  {
    CACHE_THREAD_SERIALISER();
    SCOPED_SERIALISE_CHUNK(D3D12Chunk::List_RSSetShadingRateImage);
    Serialise_RSSetShadingRateImage(ser, shadingRateImage);

    m_ListRecord->AddChunk(scope.Get());
    m_ListRecord->MarkResourceFrameReferenced(GetResID(shadingRateImage), eFrameRef_Read);
  }
}

INSTANTIATE_FUNCTION_SERIALISED(void, WrappedID3D12GraphicsCommandList, RSSetShadingRate,
                                D3D12_SHADING_RATE baseShadingRate,
                                const D3D12_SHADING_RATE_COMBINER *combiners);
INSTANTIATE_FUNCTION_SERIALISED(void, WrappedID3D12GraphicsCommandList, RSSetShadingRateImage,
                                ID3D12Resource *shadingRateImage);