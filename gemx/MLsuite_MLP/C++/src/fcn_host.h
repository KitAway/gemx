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
#ifndef _FCN_HOST_H_
#define _FCN_HOST_H_

#include "gemm_host.h"
#include "gemx_util.h"
#include "xhost.h"
using namespace std;

namespace gemx{

class FcnArgs: public kArgs {
    public:
        virtual ~FcnArgs() {}
        FcnArgs() = delete;
        FcnArgs(unsigned int p_Aoffset, unsigned int p_Boffset,
                unsigned int p_Coffset, unsigned int p_Xoffset, unsigned int p_M, unsigned int p_K,
                unsigned int p_N, unsigned int p_Lda, unsigned int p_Ldb,
                unsigned int p_Ldc, unsigned int p_Ldx, int post_scale, int post_shift, short prelu_scale, short prelu_alpha) :              
                m_fcn_args( { OpFcn, p_Aoffset, p_Boffset, p_Coffset, p_Xoffset, p_M, p_K,
                    p_N, p_Lda, p_Ldb, p_Ldc, p_Ldx, 0, 0, 0, 0 }) {
                m_fcn_args.m_postScaleVal = (post_scale << 8) | (post_shift & 0x000000ff);
                m_fcn_args.m_PReLUVal = (prelu_scale << 6) | (prelu_alpha & 0x003f);
            }
        size_t sizeInBytes() {
            return sizeof(m_fcn_args);
        }
        char *asByteArray() {
            return reinterpret_cast<char*>(&m_fcn_args);
        }

    protected:
        struct {
            int m_optype;
            unsigned int m_Aoffset, m_Boffset, m_Coffset, m_Xoffset, m_M, m_K, m_N, m_Lda, m_Ldb, m_Ldc, m_Ldx;
            int m_postScaleVal;
            short m_PReLUVal;
            short s_dummy;
            int dummy[2];
        } m_fcn_args;
};
template<typename HType>
    class FCNHost : public GEMMHost <HType>
{
    public:
        FCNHost() = delete;
        virtual ~FCNHost(){}
        FCNHost ( const FCNHost<HType>&) = delete;
        FCNHost(const string & xclbin, const string & kernelName) : GEMMHost<HType> ( xclbin, kernelName) {}

        virtual bool AddGEMMOp(const HType & A, const HType & B, const HType &C, const HType & bias, unsigned int m, unsigned int k, unsigned int n, int postScale, int postShift)
        {
            return AddFCNOp (A, B, C, bias, m, k, n, k, n, n, n, postScale, postShift, 1, 0);
        }

        virtual bool AddGEMMOp(const HType & A, const HType & B, const HType &C, const HType & bias, unsigned int m, unsigned int k, unsigned int n, unsigned int lda, unsigned int ldb, unsigned int ldc, unsigned int ldx, int postScale, int postShift) {
            return AddFCNOp (A, B, C, bias, m, k, n, k, n, n, n, postScale, postShift, 1, 0);
        }

        virtual bool AddFCNOp ( const HType & A, const HType & B, const HType &C, const HType & bias, unsigned int m, unsigned int k, unsigned int n, int postScale, int postShift, short PReLUScale, short PReLUAlpha)
        {
            return AddFCNOp ( A, B, C, bias, m, k, n, k, n, n, n,postScale, postShift, PReLUScale, PReLUAlpha);
        }

        virtual bool AddFCNOp ( const HType & A, const HType & B, const HType &C, const HType & bias, unsigned int m, unsigned int k, unsigned int n, unsigned int lda, unsigned int ldb, unsigned int ldc, unsigned int ldx, int postScale, int postShift, short PReLUScale, short PReLUAlpha)
        {
            XTimer t;
            if (this->_hostMat.find(A) == this->_hostMat.end()
                    || this->_hostMat.find(B) == this->_hostMat.end()
                    || this->_hostMat.find(C) == this->_hostMat.end()
                    || this->_hostMat.find(bias) == this->_hostMat.end()) {
                cerr << "Matrix not found!" << endl;
                return false;
            }

            unsigned long long A_off = 0, B_off = 0, C_off = 0, X_off = 0;

            xclGetMemObjDeviceAddress(this->_devHandle[A](),
                    (this->_fpga_stream->getDevice())(),
                    sizeof(unsigned long long), &A_off);
            xclGetMemObjDeviceAddress(this->_devHandle[B](),
                    (this->_fpga_stream->getDevice())(),
                    sizeof(unsigned long long), &B_off);
            xclGetMemObjDeviceAddress(this->_devHandle[C](),
                    (this->_fpga_stream->getDevice())(),
                    sizeof(unsigned long long), &C_off);

            if ( this->_devHandle.find(bias) != this->_devHandle.end()){
                xclGetMemObjDeviceAddress(this->_devHandle[bias](),
                        (this->_fpga_stream->getDevice())(),
                        sizeof(unsigned long long), &X_off);
                assert(X_off > this->_ddrDeviceBaseAddr);
                X_off -= this->_ddrDeviceBaseAddr;
            }

            assert(A_off > this->_ddrDeviceBaseAddr);
            assert(B_off > this->_ddrDeviceBaseAddr);
            assert(C_off > this->_ddrDeviceBaseAddr);
            A_off -= this->_ddrDeviceBaseAddr;
            B_off -= this->_ddrDeviceBaseAddr;
            C_off -= this->_ddrDeviceBaseAddr;

            assert(A_off % this->PAGE_SIZE == 0);
            assert(B_off % this->PAGE_SIZE == 0);
            assert(C_off % this->PAGE_SIZE == 0);
            assert(X_off % this->PAGE_SIZE == 0);

            A_off /= this->PAGE_SIZE;
            B_off /= this->PAGE_SIZE;
            C_off /= this->PAGE_SIZE;
            X_off /= this->PAGE_SIZE;

            FcnArgs args(A_off, B_off, C_off, X_off, m,
                    k, n, lda, ldb, ldc, ldx, postScale, postShift,  PReLUScale, PReLUAlpha);
            this->AddInstr ( &args);
            #ifdef GEMX_PERF_DBG
            cout << "AddFCNOp: " << t.elapsed() << endl;
            #endif
            return true;
        }

        virtual bool AddFCNDevOp ( const HType & A, const HType & B, const HType &C, const HType & bias, unsigned int m, unsigned int k, unsigned int n, int postScale, int postShift, short PReLUScale, short PReLUAlpha)
        {
            return AddFCNDevOp ( A, B, C, bias, m, k, n, k, n, n, n,postScale, postShift, PReLUScale, PReLUAlpha);
        }

        virtual bool AddFCNDevOp ( const HType & A, const HType & B, const HType &C, const HType & bias, unsigned int m, unsigned int k, unsigned int n, unsigned int lda, unsigned int ldb, unsigned int ldc, unsigned int ldx, int postScale, int postShift, short PReLUScale, short PReLUAlpha)
        {
            XTimer t;
            if (this->_hostMatPageOffset.find(A) == this->_hostMatPageOffset.end()
                    || this->_hostMatPageOffset.find(B) == this->_hostMatPageOffset.end()
                    || this->_hostMatPageOffset.find(C) == this->_hostMatPageOffset.end()
                    || this->_hostMatPageOffset.find(bias) == this->_hostMatPageOffset.end()) {
                cerr << "Matrix not found!" << endl;
                return false;
            }

            unsigned long long A_off = 0, B_off = 0, C_off = 0, X_off = 0;
            A_off = this->GetMatOffset(A);
            B_off = this->GetMatOffset(B);
            C_off = this->GetMatOffset(C);
            X_off = this->GetMatOffset(bias);

            FcnArgs args(A_off, B_off, C_off, X_off, m,
                    k, n, lda, ldb, ldc, ldx, postScale, postShift,  PReLUScale, PReLUAlpha);
            this->AddInstr ( &args);
            #ifdef GEMX_PERF_DBG
            cout << "AddFCNOp: " << t.elapsed() << endl;
            #endif
            return true;
        }

    protected:
        bool isPowerOf2( int n )
        {
            return ( (n & (n-1)) == 0 );
        }

};
}
#endif
