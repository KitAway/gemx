/**********
 * Copyright 2019 Xilinx, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * **********/
/**
 *  @brief Sparse matrix vector multiply  C += A * B
 *
 *  $DateTime: 2017/11/14 09:20:31 $
 *  $Author: Xilinx $
 */

#ifndef GEMX_SPMV_H
#define GEMX_SPMV_H

#include "assert.h"
#include "hls_stream.h"
#include "gemx_types.h"
#include "gemx_kargs.h"

namespace gemx {

//      

template <
    typename t_FloatType,
    typename t_FloatEqIntType,
    unsigned int t_DdrWidth,       // DDR width in t_FloatType
    unsigned int t_SpmvWidth,      // How many SPMV values are read and processed in parallel
    unsigned int t_kVectorBlocks,  // controls max size of the B vector
    unsigned int t_mVectorBlocks,  // GEMV max length of the C vector in t_DdrWidth-wide words  (max M)
    unsigned int t_MacGroups,      // Row groups for higher parallelism when using slow multipliers
    unsigned int t_ColAddIdxBits,  // Index bit redistribution from rows to columns
    unsigned int t_NumCblocks,     // max number of Cblocks
    unsigned int t_FloatPerDesc    // sizeof(t_FloatType) * t_FloatPerDesc == sizeof(SpmvAdesc)
  >
class Spmv
{
  private:
    static const int t_NumIdxBits = 16; // each column, and row separately
    static const unsigned int t_numDescPerDdr = t_DdrWidth / t_FloatPerDesc;
		static const unsigned int t_kVectorBlockWords = t_SpmvWidth * t_kVectorBlocks;
		static const unsigned int t_kVectorBlockEntries = t_kVectorBlockWords * t_DdrWidth;
    
  public:
    typedef WideType<t_FloatType, t_DdrWidth> DdrWideType;
    typedef hls::stream<DdrWideType> DdrWideStreamType;
    typedef SpmvArgs SpmvArgsType;
    typedef SpmvAd<t_FloatType, t_NumIdxBits, t_ColAddIdxBits, t_SpmvWidth> SpmvAdType;  // DDR-side A-type
    typedef SpmvA<t_FloatType,  t_NumIdxBits, t_ColAddIdxBits, t_SpmvWidth> SpmvAType;
    typedef SpmvAB<t_FloatType, t_NumIdxBits, t_ColAddIdxBits, t_SpmvWidth, t_MacGroups> SpmvABType;
    typedef SpmvC<t_FloatType,  t_NumIdxBits, t_ColAddIdxBits, t_SpmvWidth, t_MacGroups> SpmvCType;
    typedef hls::stream<SpmvAType> SpmvAStreamType;
    typedef hls::stream<SpmvABType> SpmvABStreamType;
    typedef hls::stream<SpmvCType> SpmvCStreamType;
    typedef WideType<SpmvAType, t_SpmvWidth> SpmvWideAType;
    typedef WideType<SpmvAdType, t_SpmvWidth> SpmvWideAdType;
    typedef WideType<SpmvABType, t_SpmvWidth> SpmvWideABType;
    typedef hls::stream<SpmvWideAType> SpmvWideAStreamType;
    typedef hls::stream<SpmvWideABType> SpmvWideABStreamType;
    typedef hls::stream<bool> ControlStreamType;
    typedef WideType<SpmvAdesc, t_numDescPerDdr> SpmvWideDType;
    static const unsigned int t_RowsInCblock = t_SpmvWidth * t_MacGroups * t_mVectorBlocks * t_DdrWidth; // capacity of m_C; it should be less than the row idx range
    static const unsigned int getRowsInCblock() {return t_RowsInCblock;}
    static const unsigned int t_NumDdrPerSpmv = t_DdrWidth / t_SpmvWidth;
    
		SpmvAdesc m_Desc[t_NumCblocks];
    t_FloatType m_B[t_SpmvWidth][t_kVectorBlocks * t_DdrWidth];
    
  private:
    t_FloatType m_C[t_SpmvWidth][t_MacGroups][t_mVectorBlocks * t_DdrWidth];
    static const unsigned int t_LcmSpmvWidthAndMacGroups = findLCM<t_SpmvWidth, t_MacGroups>::result;
    static const unsigned int t_Debug = 0;
    static const unsigned int t_Debug_xBarColSplit = 0;
    static const unsigned int t_Debug_colUnit = 0;
    static const unsigned int t_Debug_xBarRowSplit = 0;
    static const unsigned int t_Debug_xBarRowMerge = 0;
    static const unsigned int t_Debug_rowInterleave = 0;
    static const unsigned int t_Debug_rowUnit = 0;

  private:
    
    t_FloatEqIntType
    float2bits(t_FloatType p_Val) {
        union {
          t_FloatType f;
          t_FloatEqIntType b;
        } l_val;
        l_val.f = p_Val;
        return l_val.b;
      }

    t_FloatType
    getBval(unsigned int p_Col) {
        unsigned int l_colOffset = p_Col / t_SpmvWidth;
        unsigned int l_colBank = p_Col % t_SpmvWidth;
       t_FloatType l_B = m_B[l_colBank][l_colOffset];
    }
    
    SpmvWideAType
    ddrWideFloatToSpmvA(DdrWideType p_Val) {
        SpmvWideAType l_ret;
        LOOP_W:for(int w = 0; w < t_SpmvWidth; ++w) {
        
       #if GEMX_spmvPadA
          // Short
          assert(t_NumDdrPerSpmv == 4);
          t_FloatType l_Afloat = p_Val[0 + w * t_NumDdrPerSpmv];
          unsigned int l_row = float2bits(p_Val[3 + w * t_NumDdrPerSpmv]);
          unsigned int l_col = float2bits(p_Val[2 + w * t_NumDdrPerSpmv]);
		  		l_row = l_row & 0xFFFF;
		  		l_col = l_col & 0xFFFF;
          SpmvAdType l_A(l_Afloat, l_row, l_col);
       #else
          // Float
          assert(t_NumDdrPerSpmv == 2);
          t_FloatType l_Afloat = p_Val[0 + w * t_NumDdrPerSpmv];
          unsigned int l_rowCol = float2bits(p_Val[1 + w * t_NumDdrPerSpmv]);
          unsigned int l_row = l_rowCol >> 16;
          unsigned int l_col = l_rowCol & 0xFFFF;
          SpmvAdType l_A(l_Afloat, l_row, l_col);
       #endif
          l_ret[w] = SpmvAType(l_A);
        }
                
        return(l_ret);
      }
    
    void
    loaderA(DdrWideType *p_aAddr, unsigned int p_numWordsA, SpmvWideAStreamType &p_SpassOut) {
        #pragma HLS inline self off
        AREAD:for(int l_idxA = 0; l_idxA < p_numWordsA; ++l_idxA) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          DdrWideType l_valDdr = p_aAddr[l_idxA];
          SpmvWideAType l_valA = ddrWideFloatToSpmvA(l_valDdr);
          p_SpassOut.write(l_valA);
          t_Debug && std::cout << "DEBUG: loaderA " << " read " << l_valA
                                 << "\n";
        }
      }
    // Test without real ddr
    void
    loaderAnoDdr(DdrWideType *p_aAddr, unsigned int p_numWordsA, SpmvWideAStreamType &p_SpassOut) {
        #pragma HLS inline self off
        unsigned int i = 0;
        AREAD:for(int l_idxA = 0; l_idxA < p_numWordsA; ++l_idxA) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          SpmvWideAType l_val;
          LOOP_W:for(int w = 0; w < t_SpmvWidth; ++w) {
            l_val[w] = SpmvAType((i/6+w) % 65535, (i+w) % 65535, i+w);
          }
          i += t_SpmvWidth;
          p_SpassOut.write(l_val);
          t_Debug && std::cout << "DEBUG: loaderA " << " read " << l_val
                                 << "\n";
        }
      }
    
    void
    xBarColSplit(unsigned int p_numWordsA, SpmvWideAStreamType &p_Sin,
                 SpmvAStreamType p_Sout[t_SpmvWidth][t_SpmvWidth],
                 ControlStreamType &p_ScntlSplitPost) {
        #pragma HLS data_pack variable=p_Sin
        #pragma HLS STREAM    variable=p_Sin
        #pragma HLS data_pack variable=p_Sout
        #pragma HLS STREAM    variable=p_Sout

        LOOP_XC_WORDS:for(unsigned int l_idxA = 0; l_idxA < p_numWordsA; ++l_idxA) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          
          SpmvWideAType l_val = p_Sin.read();
          #pragma HLS array_partition variable=l_val COMPLETE

          LOOP_XC_W:for (int w = 0; w < t_SpmvWidth; ++w) {
            #pragma HLS UNROLL
            unsigned int l_colBank = l_val[w].getColBank();
            p_Sout[w][l_colBank].write(l_val[w]);
            t_Debug_xBarColSplit && std::cout << "DEBUG: xBarColSplit " << " read " << l_val[w]
                                 << "  and sent it to col bank " << l_colBank << "\n";
          }
        }
        p_ScntlSplitPost.write(true);
      }


      void
      xBarColMerge(SpmvAStreamType p_Sin[t_SpmvWidth][t_SpmvWidth],
                   SpmvAStreamType p_Sout[t_SpmvWidth],
                   ControlStreamType &p_ScntlMergePre,
                   ControlStreamType p_ScntlMergePost[t_SpmvWidth])
      {
        #pragma HLS data_pack variable=p_Sin
        #pragma HLS STREAM    variable=p_Sin
        #pragma HLS data_pack variable=p_Sout
        #pragma HLS STREAM    variable=p_Sout
        
        bool l_exit = false, l_preDone = false;
        BoolArr<t_SpmvWidth> l_activity(true);
        #pragma HLS array_partition variable=l_activity COMPLETE
        LOOP_XCM_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          
          if (l_preDone  && !l_activity.Or()) {
            l_exit = true;
          }
          bool l_unused;
          if (p_ScntlMergePre.read_nb(l_unused)) {
            l_preDone = true;
          }
          l_activity.Reset();
          
          LOOP_XCM_BANK_MERGE:for (int b = 0; b < t_SpmvWidth; ++b) {
            #pragma HLS UNROLL

            unsigned int l_idx = 0;
            LOOP_XCM_IDX:for (int bb = 0; bb < t_SpmvWidth; ++bb) {
              #pragma HLS UNROLL
              unsigned int l_bank = (bb + b ) % t_SpmvWidth;
              if (!p_Sin[l_bank][b].empty()) {
                l_idx = l_bank;
                break;
              }
            }
            
            SpmvAType l_val;
            if (p_Sin[l_idx][b].read_nb(l_val)) {
              p_Sout[b].write(l_val);
              l_activity[b] = true;
              t_Debug && std::cout << "DEBUG: xBarColMerge bank " << b 
                                   << " read input position " << l_idx
                                   << " value " << l_val
                                   << "  and sent it to its bank\n";
            }
          }
        }
        LOOP_XCM_SEND_EXIT:for (int b = 0; b < t_SpmvWidth; ++b) {
          #pragma HLS UNROLL
          p_ScntlMergePost[b].write(true);
        }
      }

    void
    colUnit(SpmvAStreamType &p_Sin, SpmvABStreamType &p_Sout,
            ControlStreamType &p_ScntlPre, ControlStreamType &p_ScntlPost,
            unsigned int t_BankId) {
        SpmvAType l_val;
        bool l_exit = false;
        bool l_unused = false;
        bool l_preDone = false;
        bool l_activity = true;
        LOOP_CU_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          if (p_Sin.read_nb(l_val)) {
            t_Debug_colUnit && std::cout << "DEBUG: colUnit " << t_BankId << " read     " << l_val << "\n" << std::flush;
            unsigned int l_colOffset = l_val.getColOffset();
            t_FloatType l_valB = m_B[t_BankId][l_colOffset];
            SpmvABType l_valOut(l_val.getA(), l_valB, l_val.getRow());
            t_Debug_colUnit && std::cout << "DEBUG: colUnit " << t_BankId << " computed " << l_valOut << "\n" << std::flush;
            p_Sout.write(l_valOut);
          } else {
            if (l_preDone) {
              l_exit = true;
            } else {
              if (p_ScntlPre.read_nb(l_unused)) {
                l_preDone = true;
              }
            }
          }
        }
        p_ScntlPost.write(true);
      }


    void
    xBarRowSplit(SpmvABStreamType p_Sin[t_SpmvWidth],
                 SpmvABStreamType p_Sout[t_SpmvWidth][t_SpmvWidth],
                 ControlStreamType p_ScntlSplitPre[t_SpmvWidth],
                 ControlStreamType &p_ScntlSplitPost) {
        #pragma HLS data_pack variable=p_Sin
        #pragma HLS STREAM    variable=p_Sin
        #pragma HLS data_pack variable=p_Sout
        #pragma HLS STREAM    variable=p_Sout

        bool l_exit = false;
        bool l_unused = false;
        bool l_preDone = false;
        BoolArr<t_SpmvWidth> l_activity(true), l_preActive(true);
        #pragma HLS array_partition variable=l_activity COMPLETE
        #pragma HLS array_partition variable=l_preActive COMPLETE
        LOOP_XRS_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          
          if (!l_preActive.Or() && !l_activity.Or()) {
            l_exit = true;
          }
          l_activity.Reset();

          LOOP_XRS_W:for (int w = 0; w < t_SpmvWidth; ++w) {
            #pragma HLS UNROLL
            SpmvABType l_val;
            if (p_Sin[w].read_nb(l_val)) {
              unsigned int l_rowBank = l_val.getRowBank();
              p_Sout[w][l_rowBank].write(l_val);
              l_activity[w] = true;
              t_Debug_xBarRowSplit && std::cout << "DEBUG: xBarRowSplit " << " read " << l_val
                                 << "  and sent it to row bank " << l_rowBank << "\n" << std::flush;
            }
            
            bool l_unused;
            if (p_ScntlSplitPre[w].read_nb(l_unused)) {
              l_preActive[w] = false;
            }
          }
        }
        p_ScntlSplitPost.write(true);
      }


      void
      xBarRowMerge(SpmvABStreamType p_Sin[t_SpmvWidth][t_SpmvWidth],
                   SpmvABStreamType p_Sout[t_SpmvWidth],
                   ControlStreamType &p_ScntlMergePre,
                   ControlStreamType p_ScntlMergePost[t_SpmvWidth])
      {
        #pragma HLS data_pack variable=p_Sin
        #pragma HLS STREAM    variable=p_Sin
        #pragma HLS data_pack variable=p_Sout
        #pragma HLS STREAM    variable=p_Sout

        bool l_exit = false, l_preDone = false;
        BoolArr<t_SpmvWidth> l_activity(true);
        LOOP_XRM_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          
          if (l_preDone  && !l_activity.Or()) {
            l_exit = true;
          }
          bool l_unused;
          if (p_ScntlMergePre.read_nb(l_unused)) {
            l_preDone = true;
          }
          l_activity.Reset();
          
          LOOP_XRM_BANK_MERGE:for (int b = 0; b < t_SpmvWidth; ++b) {
            #pragma HLS UNROLL

            unsigned int l_idx = 0;
            LOOP_XRM_IDX:for (int bb = 0; bb < t_SpmvWidth; ++bb) {
              #pragma HLS UNROLL
              unsigned int l_bank = (bb + b ) % t_SpmvWidth;
              if (!p_Sin[l_bank][b].empty()) {
                l_idx = l_bank;
                break;
              }
            }

            SpmvABType l_val;
            if (p_Sin[l_idx][b].read_nb(l_val)) {
              p_Sout[b].write(l_val);
              l_activity[b] = true;
              t_Debug_xBarRowMerge && std::cout << "DEBUG: xBarRowMerge bank " << b 
                                   << " read input position " << l_idx
                                   << " value " << l_val
                                   << "  and sent it to its bank\n" << std::flush;
            }
          }
        }
        LOOP_XRM_SEND_EXIT:for (int b = 0; b < t_SpmvWidth; ++b) {
          #pragma HLS UNROLL
          p_ScntlMergePost[b].write(true);
        }
      }

    void
    rowInterleave(SpmvABStreamType &p_Sin,
                  SpmvABStreamType p_Sout[t_MacGroups],
                  ControlStreamType &p_ScntlPre,
                  ControlStreamType &p_ScntlPost,
                  unsigned int t_BankId) {
        #pragma HLS data_pack variable=p_Sin
        #pragma HLS STREAM    variable=p_Sin
        #pragma HLS data_pack variable=p_Sout
        #pragma HLS STREAM    variable=p_Sout

        bool l_exit = false, l_preDone = false;
        bool l_activity = true;

        LOOP_RI_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=36870
          #pragma HLS PIPELINE
          
          if (l_preDone && !l_activity && p_Sin.empty()) {
             l_exit = true;
          }
          bool l_unused = false;
          if (p_ScntlPre.read_nb(l_unused)) {
            l_preDone = true;
          }
          l_activity = false;
          
          SpmvABType l_val;
          if (p_Sin.read_nb(l_val)) {
            unsigned int l_rowGroup = l_val.getRowGroup();
            
            unsigned int l_rowOffset = l_val.getRowOffset();
            assert(l_rowOffset < t_mVectorBlocks * t_DdrWidth);
            l_val.setRowOffsetIntoRow(l_rowOffset);
            
            p_Sout[l_rowGroup].write(l_val);
            l_activity = true;
            t_Debug_rowInterleave && std::cout << "DEBUG: rowInterleave bank " << t_BankId << " read " << l_val
                               << "  and sent it to row group " << l_rowGroup << "\n" << std::flush;
          }
        }
        p_ScntlPost.write(true);
      }


    void
    rowUnit(SpmvABStreamType p_Sin[t_MacGroups], SpmvCStreamType p_Sout[t_MacGroups],
            ControlStreamType &p_ScntlPre, ControlStreamType &p_ScntlPost,
            unsigned int t_BankId) {

        SpmvABType abVal[t_MacGroups];
        #pragma HLS array_partition variable=abVal COMPLETE
        SpmvCType gVal[t_MacGroups];
        #pragma HLS array_partition variable=gVal COMPLETE
        SpmvCType cVal[t_MacGroups];
        #pragma HLS array_partition variable=cVal COMPLETE
        
        LOOP_RU_INIT:for (int g = 0; g < t_MacGroups; ++g) {
          #pragma HLS UNROLL
          unsigned int l_initRow = t_BankId + g * t_SpmvWidth;
          gVal[g] = SpmvCType(0, l_initRow);
        }
            
        bool l_exit = false, l_preDone = false;
        bool l_activity = true;
        unsigned int l_idleCounter = 0;

        LOOP_RU_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=3072
          #pragma HLS PIPELINE
          
          if (l_preDone && !l_activity && streamsAreEmpty<SpmvABStreamType, t_MacGroups>(p_Sin)) {
             l_exit = true;
          }
          bool l_unused = false;
          if (p_ScntlPre.read_nb(l_unused)) {
            l_preDone = true;
          }
          l_activity = false;
           
          LOOP_RU_G_CALC:for (int g = 0; g < t_MacGroups; ++g) {
            #pragma HLS UNROLL
            if (p_Sin[g].read_nb(abVal[g])) {
              t_Debug_rowUnit && std::cout << "DEBUG: rowUnit " << t_BankId << " slot " << g
                                   << " read " << abVal[g] << "\n";
              cVal[g].getC() = abVal[g].getA() * abVal[g].getB();
              cVal[g].setRow(abVal[g].getRow());
              t_Debug && std::cout << "DEBUG: rowUnit " << t_BankId << " slot " << g
                       << "  multiplied " << cVal[g].getC() << " = "
                       << abVal[g].getA() << " * " << abVal[g].getB() << "\n";
              l_activity = true;
            } else {
              cVal[g].getC() = 0;
            }
            if (cVal[g].getRow() != gVal[g].getRow()) {
              p_Sout[g].write(gVal[g]);
              t_Debug && std::cout << "DEBUG: rowUnit " << t_BankId << " slot " << g
                       << "  sent out " << gVal[g] << "\n";
              gVal[g] = cVal[g];
            } else {
              gVal[g].getC() += cVal[g].getC();
              t_Debug && std::cout << "DEBUG: rowUnit " << t_BankId << " slot " << g
                       << "  added " << cVal[g] << " to local C " << gVal[g].getC() << "\n";
            }
          }
        }
        LOOP_RU_G_FLUSH:for (int g = 0; g < t_MacGroups; ++g) {
          if (gVal[g].getC() != 0) {
            p_Sout[g].write(gVal[g]);
            t_Debug && std::cout << "DEBUG: rowUnit " << t_BankId << " slot " << g
                       << "  flushed out " << gVal[g] << "\n";
          }
        }
        p_ScntlPost.write(true);
      }
    
    void
    aggUnit(SpmvCStreamType p_Sin[t_MacGroups], ControlStreamType &p_ScntlPre, unsigned int t_BankId) {

        bool l_exit = false;
        bool l_preDone = false;
        BoolArr<t_MacGroups> l_activity(true);
        #pragma HLS array_partition variable=l_activity COMPLETE

        SpmvCType cVal[t_MacGroups];
        #pragma HLS array_partition variable=cVal COMPLETE
        
        LOOP_AU_WHILE:while (!l_exit) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=604
          #pragma HLS PIPELINE II=12
          
          if (l_preDone && !l_activity.Or() && streamsAreEmpty<SpmvCStreamType, t_MacGroups>(p_Sin) ) {
            l_exit = true;
          }
          bool l_unused = false;
          if (p_ScntlPre.read_nb(l_unused)) {
            l_preDone = true;
          }
          l_activity.Reset();
          
          LOOP_AU_G_CALC:for (int g = 0; g < t_MacGroups; ++g) {
            #pragma HLS UNROLL
            if (p_Sin[g].read_nb(cVal[g])) {
              unsigned int l_rowOffset = cVal[g].getRowOffsetStoredAsRow();
              assert(l_rowOffset < t_mVectorBlocks * t_DdrWidth);
              getCref(t_BankId, g, l_rowOffset) += cVal[g].getC();
              t_Debug && std::cout << "DEBUG: aggUnit " << t_BankId << " slot " << g
                                   << "  added " << cVal[g] << " to m_C "
                                   << getCref(t_BankId, g, l_rowOffset) << "\n";
              l_activity[g] = true;
            }
          }
          
        }
      }


  public:
    
    void
    loadB(DdrWideType *p_bAddr, unsigned int p_kBlocks) {
        // Load entire B into BRAM
        assert(t_NumDdrPerSpmv * t_SpmvWidth == t_DdrWidth);
        LOOP_GEMV_BLOAD:for(unsigned int l_kBlock = 0; l_kBlock < p_kBlocks; ++l_kBlock) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=3624
          #pragma HLS pipeline
          DdrWideType l_val = p_bAddr[l_kBlock];
          LOOP_D:for(int d = 0; d < t_NumDdrPerSpmv; ++d) {
            LOOP_W:for(int w = 0; w < t_SpmvWidth; ++w) {
              unsigned int l_bank = w;
              unsigned int l_offset = d + t_NumDdrPerSpmv * l_kBlock;
              m_B[l_bank][l_offset] = l_val[w + d * t_SpmvWidth];
            }
          }
        }
      }

    void
    loadC(DdrWideType *p_cAddr, unsigned int p_mgdBlocks) {
			// Lengths in DDR words and groups respectively to store 1 lcm block
			const unsigned int t_NumFloats = t_SpmvWidth * t_MacGroups;
			const unsigned int t_ddrL = t_NumFloats / t_DdrWidth;
			assert(t_ddrL * t_DdrWidth == t_NumFloats);  
			
			unsigned int l_addIdx = 0;
			LOOP_LOAD_MGD_BLOCKS:for(unsigned int l_mgdBlock = 0; l_mgdBlock < p_mgdBlocks; ++l_mgdBlock) {
				#pragma HLS LOOP_TRIPCOUNT min=1 max=604
				#pragma HLS pipeline

				// Read
				DdrWideType l_valDdr[t_ddrL];
				LOOP_W:for(int l_di = 0; l_di < t_ddrL; ++l_di) {
					#pragma HLS UNROLL
					l_valDdr[l_di] = p_cAddr[l_addIdx] ;
					t_Debug && std::cout << "DEBUG: loadC read DdrWord " << l_valDdr[l_di]
															 << " at Cindex " << l_addIdx  << "\n";
					l_addIdx++;
				}

				// Reshape
				t_FloatType l_spmvVal[t_NumFloats];
				LOOP_DL:for(int l_di = 0; l_di < t_ddrL; ++l_di) {
					#pragma HLS UNROLL
					LOOP_D:for(int d = 0; d < t_DdrWidth; ++d) {
						#pragma HLS UNROLL
						unsigned int l_ddrIdx = l_di * t_DdrWidth + d;
						assert(l_ddrIdx < t_NumFloats);
						l_spmvVal[l_ddrIdx] = l_valDdr[l_di][d];
					}
				}
				
				// Set groups
				LOOP_S:for(int l_s = 0; l_s < t_NumFloats; ++l_s) {
					#pragma HLS UNROLL
					unsigned int l_bank = l_s % t_SpmvWidth;
					unsigned int l_group = (l_s / t_SpmvWidth) % t_MacGroups;
					unsigned int l_offset = l_mgdBlock;
					getCref(l_bank, l_group, l_offset) = l_spmvVal[l_s];
					t_Debug && std::cout << "DEBUG: loadC loaded "
															 << " bank " << l_bank
															 << " group " << l_group
															 << " offset " << l_offset
															 << " with value " << l_spmvVal[l_s]
															 << "\n";
				}
				
			}
    }
    
		void
    storeC(DdrWideType *p_cAddr, unsigned int p_mgdBlocks, bool p_pRelu) {
			// Lengths in DDR words and groups respectively to store 1 lcm block
			const unsigned int t_NumFloats = t_SpmvWidth * t_MacGroups;
			const unsigned int t_ddrL = t_NumFloats / t_DdrWidth;
			assert(t_ddrL * t_DdrWidth == t_NumFloats);  
			
			unsigned int l_addIdx = 0;
			LOOP_LOAD_MGD_BLOCKS:for(unsigned int l_mgdBlock = 0; l_mgdBlock < p_mgdBlocks; ++l_mgdBlock) {
				#pragma HLS LOOP_TRIPCOUNT min=1 max=604
				#pragma HLS pipeline

				// Get groups
				t_FloatType l_spmvVal[t_NumFloats];
				LOOP_S:for(int l_s = 0; l_s < t_NumFloats; ++l_s) {
					#pragma HLS UNROLL
					unsigned int l_bank = l_s % t_SpmvWidth;
					unsigned int l_group = (l_s / t_SpmvWidth) % t_MacGroups;
					unsigned int l_offset = l_mgdBlock;
					l_spmvVal[l_s] = getCref(l_bank, l_group, l_offset);
					t_Debug && std::cout << "DEBUG: loadC loaded "
															 << " bank " << l_bank
															 << " group " << l_group
															 << " offset " << l_offset
															 << " with value " << l_spmvVal[l_s]
															 << "\n";
				}

				// Reshape
				DdrWideType l_valDdr[t_ddrL];
				LOOP_DL:for(int l_di = 0; l_di < t_ddrL; ++l_di) {
					#pragma HLS UNROLL
					LOOP_D:for(int d = 0; d < t_DdrWidth; ++d) {
						#pragma HLS UNROLL
						unsigned int l_ddrIdx = l_di * t_DdrWidth + d;
						assert(l_ddrIdx < t_NumFloats);
						l_valDdr[l_di][d] = (p_pRelu && (l_spmvVal[l_ddrIdx] < 0))? 0: l_spmvVal[l_ddrIdx];
					}
				}
				
				// Write
				LOOP_W:for(int l_di = 0; l_di < t_ddrL; ++l_di) {
					#pragma HLS UNROLL
					p_cAddr[l_addIdx] = l_valDdr[l_di];
					t_Debug && std::cout << "DEBUG: loadC read DdrWord " << l_valDdr[l_di]
														 << " at Cindex " << l_addIdx  << "\n";
					l_addIdx++;
				}
				
			}
    }

		SpmvAdesc getDesc(unsigned int p_Cblock) {return m_Desc[p_Cblock];}
    t_FloatType &
    getCref(unsigned int p_Bank, unsigned int p_Group, unsigned int p_Offset) {
        assert(p_Bank < t_SpmvWidth);
        assert(p_Group < t_MacGroups);
        assert(p_Offset < t_mVectorBlocks * t_DdrWidth);
        return m_C[p_Bank][p_Group][p_Offset];
    }

		void
    loadB2Stream(DdrWideType *p_bAddr, DdrWideStreamType &p_outS, unsigned int p_kBlocks) {
			// Load entire B into BRAM
			assert(t_NumDdrPerSpmv * t_SpmvWidth == t_DdrWidth);
			LOOP_SPMV_BLOAD:for(unsigned int l_kBlock = 0; l_kBlock < p_kBlocks; ++l_kBlock) {
				#pragma HLS LOOP_TRIPCOUNT min=1 max=3624
				#pragma HLS pipeline
				DdrWideType l_val = p_bAddr[l_kBlock];
				p_outS.write(l_val);
			}
    }

    void storeBFromStream(DdrWideStreamType &p_inS, unsigned int p_kBlocks) {
			LOOP_SPMV_BSTORE:for(unsigned int l_kBlock = 0; l_kBlock < p_kBlocks; ++l_kBlock) {
				#pragma HLS LOOP_TRIPCOUNT min=1 max=3624
				#pragma HLS pipeline
				DdrWideType l_val;
				p_inS.read(l_val);
				LOOP_D:for(int d = 0; d < t_NumDdrPerSpmv; ++d) {
					LOOP_W:for(int w = 0; w < t_SpmvWidth; ++w) {
						unsigned int l_bank = w;
						unsigned int l_offset = d + t_NumDdrPerSpmv * l_kBlock;
						m_B[l_bank][l_offset] = l_val[w + d * t_SpmvWidth];
					}
				}
			}
		}
    
    void
    loadD(DdrWideType *p_dAddr, unsigned int p_numWordsD) {
        // Load descriptor array for C blocks into BRAM
        WideConv<DdrWideType, SpmvWideDType> l_conv;
        DREAD:for(int l_idxD = 0; l_idxD < p_numWordsD; ++l_idxD) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=32
          #pragma HLS PIPELINE
          DdrWideType l_valDdr = p_dAddr[l_idxD];
          SpmvWideDType l_wideDesc = l_conv.convert(l_valDdr);
          assert(sizeof(t_FloatType) * t_DdrWidth == sizeof(SpmvAdesc) * t_numDescPerDdr); 
          D_DDR_W:for(unsigned int w = 0; w < t_numDescPerDdr; ++w) {
            SpmvAdesc l_desc = l_wideDesc[w];
            unsigned int l_descIdx = w + t_numDescPerDdr * l_idxD;
            assert(l_descIdx < t_NumCblocks);   // If this fails then ncrease  GEMX_spmvNumCblocks
            m_Desc[l_descIdx] = l_desc;
            #pragma HLS array_partition variable=m_Desc CYCLIC factor=t_numDescPerDdr
            t_Debug && std::cout << "DEBUG: loadD " << " read m_Desc[" << l_descIdx << "] = " << l_desc
                                 << "\n";
          }
        }
      }

    void
    initC(unsigned int p_mgdBlocks) {
			// Lengths in DDR words and groups respectively to store 1 lcm block
			const unsigned int t_NumFloats = t_SpmvWidth * t_MacGroups;
			const unsigned int t_ddrL = t_NumFloats / t_DdrWidth;
			assert(t_ddrL * t_DdrWidth == t_NumFloats);  
			
			LOOP_LOAD_MGD_BLOCKS:for(unsigned int l_mgdBlock = 0; l_mgdBlock < p_mgdBlocks; ++l_mgdBlock) {
				#pragma HLS LOOP_TRIPCOUNT min=1 max=604
				#pragma HLS pipeline

				LOOP_S:for(int l_s = 0; l_s < t_NumFloats; ++l_s) {
					#pragma HLS UNROLL
					unsigned int l_bank = l_s % t_SpmvWidth;
					unsigned int l_group = (l_s / t_SpmvWidth) % t_MacGroups;
					unsigned int l_offset = l_mgdBlock;
					getCref(l_bank, l_group, l_offset) = 0;
				}
			}
    }

		void
    multA(DdrWideType *p_aAddr, unsigned int p_numWordsA) {
      static const unsigned int t_FifoDepthDeep = 16;
      static const unsigned int t_FifoDepthShallow = 1;
      
      SpmvWideAStreamType l_fifoCXinp;
      #pragma HLS data_pack variable=l_fifoCXinp
      #pragma HLS STREAM    variable=l_fifoCXinp depth=t_FifoDepthShallow
      SpmvAStreamType l_fifoCXsplit[t_SpmvWidth][t_SpmvWidth];
      #pragma HLS data_pack variable=l_fifoCXsplit
      #pragma HLS STREAM    variable=l_fifoCXsplit depth=t_FifoDepthDeep
      SpmvAStreamType l_fifoCUinp[t_SpmvWidth];
      #pragma HLS data_pack variable=l_fifoCUinp
      #pragma HLS STREAM    variable=l_fifoCUinp depth=t_FifoDepthShallow
      SpmvABStreamType l_fifoRXinp[t_SpmvWidth];
      #pragma HLS data_pack variable=l_fifoRXinp
      #pragma HLS STREAM    variable=l_fifoRXinp depth=t_FifoDepthShallow
      SpmvABStreamType l_fifoRXsplit[t_SpmvWidth][t_SpmvWidth];
      #pragma HLS data_pack variable=l_fifoRXsplit
      #pragma HLS STREAM    variable=l_fifoRXsplit depth=t_FifoDepthDeep
      SpmvABStreamType l_fifoRXmerged[t_SpmvWidth];
      #pragma HLS data_pack variable=l_fifoRXmerged
      #pragma HLS STREAM    variable=l_fifoRXmerged depth=t_FifoDepthDeep
      SpmvABStreamType l_fifoRIout[t_SpmvWidth][t_MacGroups];
      #pragma HLS data_pack variable=l_fifoRIout
      #pragma HLS STREAM    variable=l_fifoRIout  depth=t_FifoDepthDeep
      SpmvCStreamType l_fifoRUout[t_SpmvWidth][t_MacGroups];
      #pragma HLS data_pack variable=l_fifoRUout
      #pragma HLS STREAM    variable=l_fifoRUout  depth=t_FifoDepthShallow

      ControlStreamType l_controlCXsplitDone;
      #pragma HLS data_pack variable=l_controlCXsplitDone
      #pragma HLS STREAM    variable=l_controlCXsplitDone

      ControlStreamType l_controlCUpre[t_SpmvWidth];
      #pragma HLS data_pack variable=l_controlCUpre
      #pragma HLS STREAM    variable=l_controlCUpre

      ControlStreamType l_controlCUpost[t_SpmvWidth];
      #pragma HLS data_pack variable=l_controlCUpost
      #pragma HLS STREAM    variable=l_controlCUpost

      ControlStreamType l_controlRXsplitDone;
      #pragma HLS data_pack variable=l_controlRXsplitDone
      #pragma HLS STREAM    variable=l_controlRXsplitDone
      ControlStreamType l_controlRXmergeDone[t_SpmvWidth];
      #pragma HLS data_pack variable=l_controlRXmergeDone
      #pragma HLS STREAM    variable=l_controlRXmergeDone

      ControlStreamType l_controlRiDone[t_SpmvWidth];
      #pragma HLS data_pack variable=l_controlRiDone
      #pragma HLS STREAM    variable=l_controlRiDone
      
      ControlStreamType l_controlRUpost[t_SpmvWidth];
      #pragma HLS data_pack variable=l_controlRUpost
      #pragma HLS STREAM    variable=l_controlRUpost
      
      #pragma HLS array_partition variable=m_B dim=1 COMPLETE
      #pragma HLS array_partition variable=m_C dim=1 COMPLETE
      #pragma HLS array_partition variable=m_C dim=2 COMPLETE
      #pragma HLS array_partition variable=l_fifoCXsplit COMPLETE dim=1
      #pragma HLS array_partition variable=l_fifoCXsplit COMPLETE dim=2
      #pragma HLS array_partition variable=l_fifoRIout COMPLETE dim=1
      #pragma HLS array_partition variable=l_fifoRIout COMPLETE dim=2
      #pragma HLS array_partition variable=l_fifoRUout COMPLETE dim=1
      #pragma HLS array_partition variable=l_fifoRUout COMPLETE dim=2
      #pragma HLS array_partition variable=l_fifoCUinp COMPLETE
      #pragma HLS array_partition variable=l_controlCUpre COMPLETE
      #pragma HLS array_partition variable=l_controlCUpost COMPLETE
      #pragma HLS array_partition variable=l_controlRUpost COMPLETE
      #pragma HLS array_partition variable=l_controlRiDone COMPLETE
      
      #pragma HLS DATAFLOW
      
      loaderA(p_aAddr, p_numWordsA, l_fifoCXinp);
      
      xBarColSplit(p_numWordsA, l_fifoCXinp, l_fifoCXsplit, l_controlCXsplitDone);
      xBarColMerge(l_fifoCXsplit, l_fifoCUinp, l_controlCXsplitDone, l_controlCUpre);
      
      LOOP_W_CU:for(int w = 0; w < t_SpmvWidth; ++w) {
        #pragma HLS UNROLL
        colUnit(l_fifoCUinp[w], l_fifoRXinp[w], l_controlCUpre[w], l_controlCUpost[w], w);
      }
      
      xBarRowSplit(l_fifoRXinp, l_fifoRXsplit, l_controlCUpost, l_controlRXsplitDone);
      xBarRowMerge(l_fifoRXsplit, l_fifoRXmerged, l_controlRXsplitDone, l_controlRXmergeDone);

      LOOP_W_RU:for(int w = 0; w < t_SpmvWidth; ++w) {
        #pragma HLS UNROLL
        rowInterleave(l_fifoRXmerged[w], l_fifoRIout[w], l_controlRXmergeDone[w],
                      l_controlRiDone[w], w);
        rowUnit(l_fifoRIout[w], l_fifoRUout[w], l_controlRiDone[w], l_controlRUpost[w], w);
        aggUnit(l_fifoRUout[w], l_controlRUpost[w], w);
     }
    }

    void
    storeCandStreaming(DdrWideType *p_cAddr, DdrWideStreamType &p_outS, unsigned int p_mgdBlocks) {
        
        // Lengths in DDR words and groups respectively to store 1 lcm block
        const unsigned int t_NumFloats = t_SpmvWidth * t_MacGroups;
        const unsigned int t_ddrL = t_NumFloats / t_DdrWidth;
        assert(t_ddrL * t_DdrWidth == t_NumFloats);  
        
        unsigned int l_addIdx = 0;
        LOOP_LOAD_MGD_BLOCKS:for(unsigned int l_mgdBlock = 0; l_mgdBlock < p_mgdBlocks; ++l_mgdBlock) {
          #pragma HLS LOOP_TRIPCOUNT min=1 max=604
          #pragma HLS pipeline

          // Get groups
          t_FloatType l_spmvVal[t_NumFloats];
          LOOP_S:for(int l_s = 0; l_s < t_NumFloats; ++l_s) {
            #pragma HLS UNROLL
            unsigned int l_bank = l_s % t_SpmvWidth;
            unsigned int l_group = (l_s / t_SpmvWidth) % t_MacGroups;
            unsigned int l_offset = l_mgdBlock;
            l_spmvVal[l_s] = getCref(l_bank, l_group, l_offset);
            t_Debug && std::cout << "DEBUG: loadC loaded "
                                 << " bank " << l_bank
                                 << " group " << l_group
                                 << " offset " << l_offset
                                 << " with value " << l_spmvVal[l_s]
                                 << "\n";
          }

          // Reshape
          DdrWideType l_valDdr[t_ddrL];
          LOOP_DL:for(int l_di = 0; l_di < t_ddrL; ++l_di) {
            #pragma HLS UNROLL
            LOOP_D:for(int d = 0; d < t_DdrWidth; ++d) {
              #pragma HLS UNROLL
              unsigned int l_ddrIdx = l_di * t_DdrWidth + d;
              assert(l_ddrIdx < t_NumFloats);
              l_valDdr[l_di][d] = l_spmvVal[l_ddrIdx];
            }
          }
          
          // Write
          LOOP_W:for(int l_di = 0; l_di < t_ddrL; ++l_di) { 
            p_cAddr[l_addIdx] = l_valDdr[l_di];
						p_outS.write(l_valDdr[l_di]);
            t_Debug && std::cout << "DEBUG: loadC read DdrWord " << l_valDdr[l_di]
                                 << " at Cindex " << l_addIdx  << "\n";
            l_addIdx++;
          }
          
        }
      }

    void runSpmv(
        DdrWideType *p_DdrRd,
        DdrWideType *p_DdrWr,
        SpmvArgsType &p_Args
      ) {
        
        t_Debug && std::cout << "\nrunSpmv START M=" << p_Args.m_M << " K=" << p_Args.m_K << "\n";
        #pragma HLS inline off
       	#pragma HLS RESOURCE variable=m_Desc core=XPM_MEMORY uram
				#pragma HLS DATA_PACK variable=m_Desc 
        // Load entire B into BRAM
        const unsigned int l_kBlocks = p_Args.m_Bblocks;
				bool l_pRelu = p_Args.m_Prelu;
        
        // Load C block descriptors
        const unsigned int l_Cblocks = p_Args.m_Cblocks;
        const unsigned int l_descDdrWords = (l_kBlocks*l_Cblocks + t_numDescPerDdr - 1) / t_numDescPerDdr;
        DdrWideType *l_dAddr = p_DdrRd + p_Args.m_Aoffset * DdrWideType::per4k();
        loadD(l_dAddr, l_descDdrWords);  // in descriptor units
        
				unsigned int l_Bblock=0;
				while (l_Bblock < l_kBlocks) {
					DdrWideType *l_bAddr = p_DdrRd + p_Args.m_Boffset * DdrWideType::per4k() + l_Bblock * t_kVectorBlockWords;
					const unsigned int l_kLoadBlocks = ((l_Bblock < l_kBlocks-1) || ((p_Args.m_K % t_kVectorBlockEntries) == 0))?
																							 t_kVectorBlockWords: (p_Args.m_K % t_kVectorBlockEntries) / t_DdrWidth;
					loadB(l_bAddr, l_kLoadBlocks); // in DDR units
					for (unsigned int l_Cblock = 0; l_Cblock < l_Cblocks; ++l_Cblock) {
						SpmvAdesc l_desc = getDesc(l_Bblock * l_Cblocks + l_Cblock);
						unsigned int l_nnz = l_desc.getNnz();
						const unsigned int t_mgdBlocks = t_RowsInCblock / (t_SpmvWidth * t_MacGroups);
						assert(t_mgdBlocks *  (t_SpmvWidth * t_MacGroups) == t_RowsInCblock);
						const unsigned int l_mgdBlocks = ((l_Cblock < l_Cblocks - 1) || ((p_Args.m_M % t_RowsInCblock)==0))?
																								t_mgdBlocks :
																								(p_Args.m_M % t_RowsInCblock) / (t_SpmvWidth * t_MacGroups);
						assert((l_mgdBlocks == t_mgdBlocks) ||
									 (l_mgdBlocks *  (t_SpmvWidth * t_MacGroups) == (p_Args.m_M % t_RowsInCblock)));

						// Load C
						DdrWideType *l_cAddr = p_DdrWr + p_Args.m_Coffset * DdrWideType::per4k() +
																	 l_Cblock * (t_RowsInCblock / t_DdrWidth) ;
						loadC(l_cAddr, l_mgdBlocks);

						unsigned int l_blockAoffset = l_desc.getOffset();
						DdrWideType *l_aAddr = p_DdrRd + (p_Args.m_Aoffset + p_Args.m_DescPages + l_blockAoffset) *
																	 DdrWideType::per4k();
						const unsigned int l_numWordsA = l_nnz * t_NumDdrPerSpmv / t_DdrWidth;
						assert(l_numWordsA * t_DdrWidth == l_nnz * t_NumDdrPerSpmv);
						multA(l_aAddr, l_numWordsA);

						// Store C
						storeC(l_cAddr, l_mgdBlocks, l_pRelu);
					}
					l_Bblock++;
				}
      }
      static const unsigned int getSpmvWidth() {return t_SpmvWidth;}
      static const unsigned int getDdrWidth() {return t_DdrWidth;}
      
};

} // namespace
#endif

