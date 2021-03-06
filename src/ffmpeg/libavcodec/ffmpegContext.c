/*
 * (C) 2006-2012 see AUTHORS
 *
 * This file is part of mplayerc.
 *
 * Mplayerc is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mplayerc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define H264_MERGE_TESTING

#include <windows.h>
#include <winnt.h>

#include "ffmpegContext.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "golomb.h"
#include "h264.h"
#include "h264data.h"
#include "vc1.h"

#define VFW_E_INVALID_FILE_FORMAT        ((HRESULT)0x8004022FL)

int av_h264_decode_frame(struct AVCodecContext* avctx, int* nOutPOC, int64_t* rtStartTime, uint8_t *buf, int buf_size);
int av_vc1_decode_frame(AVCodecContext *avctx, uint8_t *buf, int buf_size, int *nFrameSize);
void av_init_packet(AVPacket *pkt);

const byte ZZ_SCAN[16]  = {
    0,  1,  4,  8,
    5,  2,  3,  6,
    9, 12, 13, 10,
    7, 11, 14, 15
};

const byte ZZ_SCAN8[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

int IsVistaOrAbove()
{
    OSVERSIONINFO osver;

    osver.dwOSVersionInfoSize = sizeof( OSVERSIONINFO );

    if (    GetVersionEx( &osver ) &&
            osver.dwPlatformId == VER_PLATFORM_WIN32_NT &&
            (osver.dwMajorVersion >= 6 ) )
        return 1;

    return 0;
}

char* GetFFMpegPictureType(int nType)
{
    static char*    s_FFMpegPictTypes[] = { "? ", "I ", "P ", "B ", "S ", "SI", "SP" };
    int     nTypeCount = sizeof(s_FFMpegPictTypes)/sizeof(TCHAR)-1;

    return s_FFMpegPictTypes[min(nType, nTypeCount)];
}

inline MpegEncContext* GetMpegEncContext(struct AVCodecContext* pAVCtx)
{
    MpegEncContext*     s = NULL;

    switch (pAVCtx->codec_id) {
        case AV_CODEC_ID_VC1 :
        case AV_CODEC_ID_H264 :
            s = (MpegEncContext*) pAVCtx->priv_data;
            break;
    }
    return s;
}

int FFH264DecodeBuffer (struct AVCodecContext* pAVCtx, BYTE* pBuffer, UINT nSize, int* pFramePOC, int* pOutPOC, REFERENCE_TIME* pOutrtStart)
{
    int result = -1;
    if (pBuffer != NULL) {
        H264Context* h = (H264Context*) pAVCtx->priv_data;
        result = av_h264_decode_frame (pAVCtx, pOutPOC, pOutrtStart, pBuffer, nSize);

        if (result != -1 && h->s.current_picture_ptr != NULL && pFramePOC) {
            *pFramePOC = h->s.current_picture_ptr->poc;
        }
    }
    return result;
}

// returns TRUE if version is equal to or higher than A.B.C.D, returns FALSE otherwise
BOOL DriverVersionCheck(LARGE_INTEGER VideoDriverVersion, int A, int B, int C, int D)
{
    if (HIWORD(VideoDriverVersion.HighPart) > A) {
        return TRUE;
    } else if (HIWORD(VideoDriverVersion.HighPart) == A) {
        if (LOWORD(VideoDriverVersion.HighPart) > B) {
            return TRUE;
        } else if (LOWORD(VideoDriverVersion.HighPart) == B) {
            if (HIWORD(VideoDriverVersion.LowPart) > C) {
                return TRUE;
            } else if (HIWORD(VideoDriverVersion.LowPart) == C) {
                if (LOWORD(VideoDriverVersion.LowPart) >= D) {
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

int FFH264CheckCompatibility(int nWidth, int nHeight, struct AVCodecContext* pAVCtx, BYTE* pBuffer, UINT nSize, DWORD nPCIVendor, DWORD nPCIDevice, LARGE_INTEGER VideoDriverVersion)
{
    H264Context*    pContext = (H264Context*) pAVCtx->priv_data;
    SPS*            cur_sps;
    PPS*            cur_pps;

    int video_is_level51            = 0;
    int no_level51_support          = 1;
    int too_much_ref_frames         = 0;
    int profile_higher_than_high    = 0;
    int max_ref_frames_dpb41        = min(11, 8388608/(nWidth * nHeight) );

    if (pBuffer != NULL) {
        av_h264_decode_frame (pAVCtx, NULL, NULL, pBuffer, nSize);
    }

    cur_sps = pContext->sps_buffers[0];
    cur_pps = pContext->pps_buffers[0];

    if (cur_sps != NULL) {
        int max_ref_frames = 0;
        video_is_level51 = cur_sps->level_idc >= 51 ? 1 : 0;
        profile_higher_than_high = (cur_sps->profile_idc > 100);
        max_ref_frames = max_ref_frames_dpb41; // default value is calculate

        if (nPCIVendor == PCIV_nVidia) {
            // nVidia cards support level 5.1 since drivers v6.14.11.7800 for XP and drivers v7.15.11.7800 for Vista/7
            if (IsVistaOrAbove()) {
                if (DriverVersionCheck(VideoDriverVersion, 7, 15, 11, 7800)) {
                    no_level51_support = 0;

                    // max ref frames is 16 for HD and 11 otherwise
                    if (nWidth >= 1280) {
                        max_ref_frames = 16;
                    } else {
                        max_ref_frames = 11;
                    }
                }
            } else {
                if (DriverVersionCheck(VideoDriverVersion, 6, 14, 11, 7800)) {
                    no_level51_support = 0;

                    // max ref frames is 14
                    max_ref_frames = 14;
                }
            }
        } else if (nPCIVendor == PCIV_S3_Graphics) {
            no_level51_support = 0;
        } else if (nPCIVendor == PCIV_ATI) {
            // HD4xxx, HD5xxx, and HD6xxx AMD/ATI cards support level 5.1 since drivers v8.14.1.6105 (Catalyst 10.4)
            if (nPCIDevice > 0x6700) {
                if (DriverVersionCheck(VideoDriverVersion, 8, 14, 1, 6105)) {
                    no_level51_support = 0;
                    max_ref_frames = 16;
                }
            }
        }

        // Check maximum allowed number reference frames
        if (cur_sps->ref_frame_count > max_ref_frames) {
            too_much_ref_frames = 1;
        }
    }

    return (video_is_level51 * no_level51_support * DXVA_UNSUPPORTED_LEVEL) + (too_much_ref_frames * DXVA_TOO_MANY_REF_FRAMES) + (profile_higher_than_high * DXVA_PROFILE_HIGHER_THAN_HIGH);
}

void CopyScalingMatrix(DXVA_Qmatrix_H264* pDest, PPS* pps, DWORD nPCIVendor)
{
    int i, j;
    memset(pDest, 0, sizeof(DXVA_Qmatrix_H264));
    if (nPCIVendor == PCIV_ATI) {
        for (i = 0; i < 6; i++)
            for (j = 0; j < 16; j++)
                pDest->bScalingLists4x4[i][j] = pps->scaling_matrix4[i][j];

        for (i = 0; i < 64; i++) {
            pDest->bScalingLists8x8[0][i] = pps->scaling_matrix8[0][i];
            pDest->bScalingLists8x8[1][i] = pps->scaling_matrix8[3][i];
        }
    } else {
        for (i = 0; i < 6; i++)
            for (j = 0; j < 16; j++)
                pDest->bScalingLists4x4[i][j] = pps->scaling_matrix4[i][ZZ_SCAN[j]];

        for (i = 0; i < 64; i++) {
            pDest->bScalingLists8x8[0][i] = pps->scaling_matrix8[0][ZZ_SCAN8[i]];
            pDest->bScalingLists8x8[1][i] = pps->scaling_matrix8[3][ZZ_SCAN8[i]];
        }
    }
}

USHORT FFH264FindRefFrameIndex(USHORT num_frame, DXVA_PicParams_H264* pDXVAPicParams)
{
    int i;
    for (i=0; i<pDXVAPicParams->num_ref_frames; i++) {
        if (pDXVAPicParams->FrameNumList[i] == num_frame) {
            return pDXVAPicParams->RefFrameList[i].Index7Bits;
        }
    }

#ifdef _DEBUG
    //  DebugBreak();       // Ref frame not found !
#endif

    return 127;
}

HRESULT FFH264BuildPicParams (DXVA_PicParams_H264* pDXVAPicParams, DXVA_Qmatrix_H264* pDXVAScalingMatrix, int* nFieldType, int* nSliceType, struct AVCodecContext* pAVCtx, DWORD nPCIVendor)
{
    H264Context*            h = (H264Context*) pAVCtx->priv_data;
    SPS*                    cur_sps;
    PPS*                    cur_pps;
    MpegEncContext* const   s = &h->s;
    int                     field_pic_flag;
    HRESULT                 hr = E_FAIL;
    const                   Picture *current_picture = s->current_picture_ptr;

    field_pic_flag = (h->s.picture_structure != PICT_FRAME);

    cur_sps = &h->sps;
    cur_pps = &h->pps;

    if (cur_sps && cur_pps) {
        *nFieldType = h->s.picture_structure;
        if (h->sps.pic_struct_present_flag) {
            switch (h->sei_pic_struct) {
                case SEI_PIC_STRUCT_TOP_FIELD:
                case SEI_PIC_STRUCT_TOP_BOTTOM:
                case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
                    *nFieldType = PICT_TOP_FIELD;
                    break;
                case SEI_PIC_STRUCT_BOTTOM_FIELD:
                case SEI_PIC_STRUCT_BOTTOM_TOP:
                case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
                    *nFieldType = PICT_BOTTOM_FIELD;
                    break;
                case SEI_PIC_STRUCT_FRAME_DOUBLING:
                case SEI_PIC_STRUCT_FRAME_TRIPLING:
                case SEI_PIC_STRUCT_FRAME:
                    *nFieldType = PICT_FRAME;
                    break;
            }
        }

        *nSliceType = h->slice_type;

        if (cur_sps->mb_width==0 || cur_sps->mb_height==0) {
            return VFW_E_INVALID_FILE_FORMAT;
        }

        pDXVAPicParams->wFrameWidthInMbsMinus1                  = cur_sps->mb_width  - 1;       // pic_width_in_mbs_minus1;
        pDXVAPicParams->wFrameHeightInMbsMinus1                 = cur_sps->mb_height * (2 - cur_sps->frame_mbs_only_flag) - 1;      // pic_height_in_map_units_minus1;
        pDXVAPicParams->num_ref_frames                          = cur_sps->ref_frame_count;     // num_ref_frames;
        pDXVAPicParams->field_pic_flag                          = field_pic_flag;
        pDXVAPicParams->MbaffFrameFlag                          = (h->sps.mb_aff && (field_pic_flag==0));
        pDXVAPicParams->residual_colour_transform_flag          = cur_sps->residual_color_transform_flag;
        pDXVAPicParams->sp_for_switch_flag                      = h->sp_for_switch_flag;
        pDXVAPicParams->chroma_format_idc                       = cur_sps->chroma_format_idc;
        pDXVAPicParams->RefPicFlag                              = h->ref_pic_flag;
        pDXVAPicParams->constrained_intra_pred_flag             = cur_pps->constrained_intra_pred;
        pDXVAPicParams->weighted_pred_flag                      = cur_pps->weighted_pred;
        pDXVAPicParams->weighted_bipred_idc                     = cur_pps->weighted_bipred_idc;
        pDXVAPicParams->frame_mbs_only_flag                     = cur_sps->frame_mbs_only_flag;
        pDXVAPicParams->transform_8x8_mode_flag                 = cur_pps->transform_8x8_mode;
        pDXVAPicParams->MinLumaBipredSize8x8Flag                = h->sps.level_idc >= 31;
        pDXVAPicParams->IntraPicFlag                            = (h->slice_type == AV_PICTURE_TYPE_I );

        pDXVAPicParams->bit_depth_luma_minus8                   = cur_sps->bit_depth_luma   - 8;    // bit_depth_luma_minus8
        pDXVAPicParams->bit_depth_chroma_minus8                 = cur_sps->bit_depth_chroma - 8;    // bit_depth_chroma_minus8

        //  pDXVAPicParams->StatusReportFeedbackNumber              = SET IN DecodeFrame;
        //  pDXVAPicParams->CurrFieldOrderCnt                       = SET IN UpdateRefFramesList;
        //  pDXVAPicParams->FieldOrderCntList                       = SET IN UpdateRefFramesList;
        //  pDXVAPicParams->FrameNumList                            = SET IN UpdateRefFramesList;
        //  pDXVAPicParams->UsedForReferenceFlags                   = SET IN UpdateRefFramesList;
        //  pDXVAPicParams->NonExistingFrameFlags

        pDXVAPicParams->frame_num                               = h->frame_num;

        pDXVAPicParams->log2_max_frame_num_minus4               = cur_sps->log2_max_frame_num - 4;                  // log2_max_frame_num_minus4;
        pDXVAPicParams->pic_order_cnt_type                      = cur_sps->poc_type;                                // pic_order_cnt_type;
        if (cur_sps->poc_type == 0)
            pDXVAPicParams->log2_max_pic_order_cnt_lsb_minus4   = cur_sps->log2_max_poc_lsb - 4;                    // log2_max_pic_order_cnt_lsb_minus4;
        else if (cur_sps->poc_type == 1)
            pDXVAPicParams->delta_pic_order_always_zero_flag    = cur_sps->delta_pic_order_always_zero_flag;
        pDXVAPicParams->direct_8x8_inference_flag               = cur_sps->direct_8x8_inference_flag;
        pDXVAPicParams->entropy_coding_mode_flag                = cur_pps->cabac;                                   // entropy_coding_mode_flag;
        pDXVAPicParams->pic_order_present_flag                  = cur_pps->pic_order_present;                       // pic_order_present_flag;
        pDXVAPicParams->num_slice_groups_minus1                 = cur_pps->slice_group_count - 1;                   // num_slice_groups_minus1;
        pDXVAPicParams->slice_group_map_type                    = cur_pps->mb_slice_group_map_type;                 // slice_group_map_type;
        pDXVAPicParams->deblocking_filter_control_present_flag  = cur_pps->deblocking_filter_parameters_present;    // deblocking_filter_control_present_flag;
        pDXVAPicParams->redundant_pic_cnt_present_flag          = cur_pps->redundant_pic_cnt_present;               // redundant_pic_cnt_present_flag;

        pDXVAPicParams->chroma_qp_index_offset                  = cur_pps->chroma_qp_index_offset[0];
        pDXVAPicParams->second_chroma_qp_index_offset           = cur_pps->chroma_qp_index_offset[1];
        pDXVAPicParams->num_ref_idx_l0_active_minus1            = cur_pps->ref_count[0]-1;                          // num_ref_idx_l0_active_minus1;
        pDXVAPicParams->num_ref_idx_l1_active_minus1            = cur_pps->ref_count[1]-1;                          // num_ref_idx_l1_active_minus1;
        pDXVAPicParams->pic_init_qp_minus26                     = cur_pps->init_qp - 26;
        pDXVAPicParams->pic_init_qs_minus26                     = cur_pps->init_qs - 26;

        pDXVAPicParams->CurrPic.AssociatedFlag = field_pic_flag && (h->s.picture_structure == PICT_BOTTOM_FIELD);
        pDXVAPicParams->CurrFieldOrderCnt[0] = 0;
        if ((h->s.picture_structure & PICT_TOP_FIELD) && current_picture->field_poc[0] != INT_MAX) {
            pDXVAPicParams->CurrFieldOrderCnt[0] = current_picture->field_poc[0];
        }
        pDXVAPicParams->CurrFieldOrderCnt[1] = 0;
        if ((h->s.picture_structure & PICT_BOTTOM_FIELD) && current_picture->field_poc[1] != INT_MAX) {
            pDXVAPicParams->CurrFieldOrderCnt[1] = current_picture->field_poc[1];
        }

        CopyScalingMatrix (pDXVAScalingMatrix, cur_pps, nPCIVendor);
        hr = S_OK;
    }

    return hr;
}

void FFH264SetCurrentPicture (int nIndex, DXVA_PicParams_H264* pDXVAPicParams, struct AVCodecContext* pAVCtx)
{
    H264Context* h = (H264Context*) pAVCtx->priv_data;

    pDXVAPicParams->CurrPic.Index7Bits      = nIndex;
    if (h->s.current_picture_ptr) {
        h->s.current_picture_ptr->f.opaque  = (void*)nIndex;
    }
}

void FFH264UpdateRefFramesList (DXVA_PicParams_H264* pDXVAPicParams, struct AVCodecContext* pAVCtx)
{
    H264Context*    h = (H264Context*) pAVCtx->priv_data;
    UINT            nUsedForReferenceFlags = 0;
    int             i, j;
    Picture*        pic;
    UCHAR           AssociatedFlag;

    for (i=0, j=0; i<16; i++) {
        if (i < h->short_ref_count) {
            // Short list reference frames
            pic             = h->short_ref[h->short_ref_count - i - 1];
            AssociatedFlag  = pic->long_ref != 0;
        } else {
            // Long list reference frames
            pic = NULL;
            while (!pic && j < h->short_ref_count + 16) {
                pic = h->long_ref[j++ - h->short_ref_count];
            }
            AssociatedFlag  = 1;
        }

        if (pic != NULL) {
            pDXVAPicParams->FrameNumList[i]                 = pic->long_ref ? pic->pic_id : pic->frame_num;
            pDXVAPicParams->FieldOrderCntList[i][0]         = 0;
            pDXVAPicParams->FieldOrderCntList[i][1]         = 0;

            if (pic->field_poc[0] != INT_MAX) {
                pDXVAPicParams->FieldOrderCntList[i][0]     = pic->field_poc [0];
                nUsedForReferenceFlags                      |= 1<<(i*2);
            }

            if (pic->field_poc[1] != INT_MAX) {
                pDXVAPicParams->FieldOrderCntList[i][1]     = pic->field_poc [1];
                nUsedForReferenceFlags                      |= 2<<(i*2);
            }

            pDXVAPicParams->RefFrameList[i].AssociatedFlag  = AssociatedFlag;
            pDXVAPicParams->RefFrameList[i].Index7Bits      = (UCHAR)pic->f.opaque;
        } else {
            pDXVAPicParams->FrameNumList[i]                 = 0;
            pDXVAPicParams->FieldOrderCntList[i][0]         = 0;
            pDXVAPicParams->FieldOrderCntList[i][1]         = 0;
            pDXVAPicParams->RefFrameList[i].AssociatedFlag  = 1;
            pDXVAPicParams->RefFrameList[i].Index7Bits      = 127;
        }
    }

    pDXVAPicParams->UsedForReferenceFlags = nUsedForReferenceFlags;
}

BOOL FFH264IsRefFrameInUse (int nFrameNum, struct AVCodecContext* pAVCtx)
{
    H264Context*    h = (H264Context*) pAVCtx->priv_data;
    int             i;

    for (i=0; i<h->short_ref_count; i++) {
        if ((int)h->short_ref[i]->f.opaque == nFrameNum) {
            return TRUE;
        }
    }

    for (i=0; i<h->long_ref_count; i++) {
        if ((int)h->long_ref[i]->f.opaque == nFrameNum) {
            return TRUE;
        }
    }

    return FALSE;
}

void FF264UpdateRefFrameSliceLong(DXVA_PicParams_H264* pDXVAPicParams, DXVA_Slice_H264_Long* pSlice, struct AVCodecContext* pAVCtx)
{
    H264Context*            h   = (H264Context*) pAVCtx->priv_data;
    MpegEncContext* const   s   = &h->s;
    HRESULT                 hr  = E_FAIL;
    unsigned                i, list;

    for (list = 0; list < 2; list++) {
        for (i = 0; i < 32; i++) {
            pSlice->RefPicList[list][i].AssociatedFlag  = 1;
            pSlice->RefPicList[list][i].Index7Bits      = 127;
            pSlice->RefPicList[list][i].bPicEntry       = 255;
        }
    }

    for (list = 0; list < 2; list++) {
        for (i = 0; i < 32; i++) {
            if (list < h->list_count && i < h->ref_count[list]) {
                const Picture *r = &h->ref_list[list][i];
                pSlice->RefPicList[list][i].Index7Bits      = FFH264FindRefFrameIndex (h->ref_list[list][i].frame_num, pDXVAPicParams);
                pSlice->RefPicList[list][i].AssociatedFlag  = r->f.reference == PICT_BOTTOM_FIELD;
            }
        }
    }
}

void FFH264SetDxvaSliceLong (struct AVCodecContext* pAVCtx, void* pSliceLong)
{
    H264Context* h      = (H264Context*) pAVCtx->priv_data;
    h->dxva_slice_long  = pSliceLong;
}

HRESULT FFVC1UpdatePictureParam (DXVA_PictureParameters* pPicParams, struct AVCodecContext* pAVCtx, int* nFieldType, int* nSliceType, BYTE* pBuffer, UINT nSize, UINT* nFrameSize, BOOL b_SecondField, BOOL* b_repeat_pict)
{
    VC1Context* vc1     = (VC1Context*) pAVCtx->priv_data;
    int out_nFrameSize  = 0;

    if (pBuffer && !b_SecondField) {
        av_vc1_decode_frame (pAVCtx, pBuffer, nSize, &out_nFrameSize);
    }

    // WARNING : vc1->interlace is not reliable (always set for progressive video on HD-DVD material)
    if (vc1->fcm == 0) {
        if (nFieldType) {
            *nFieldType = PICT_FRAME;
        }
    } else {    // fcm : 2 or 3 frame or field interlaced
        if (nFieldType) {
            *nFieldType = (vc1->tff ? PICT_TOP_FIELD : PICT_BOTTOM_FIELD);
        }
    }

    if (b_SecondField) {
        vc1->second_field = 1;
        vc1->s.picture_structure = PICT_TOP_FIELD + vc1->tff;
        vc1->s.pict_type = (vc1->fptype & 1) ? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
        if (vc1->fptype & 4)
            vc1->s.pict_type = (vc1->fptype & 1) ? AV_PICTURE_TYPE_BI : AV_PICTURE_TYPE_B;
    }

    if (nFrameSize) {
        *nFrameSize = out_nFrameSize;
    }

    if (vc1->profile == PROFILE_ADVANCED) {
        /* It is the cropped width/height -1 of the frame */
        pPicParams->wPicWidthInMBminus1 = pAVCtx->width  - 1;
        pPicParams->wPicHeightInMBminus1= pAVCtx->height - 1;
    } else {
        /* It is the coded width/height in macroblock -1 of the frame */
        pPicParams->wPicWidthInMBminus1 = vc1->s.mb_width  - 1;
        pPicParams->wPicHeightInMBminus1= vc1->s.mb_height - 1;
    }

    pPicParams->bSecondField            = (vc1->interlace && vc1->fcm == ILACE_FIELD && vc1->second_field);
    pPicParams->bPicIntra               = vc1->s.pict_type == AV_PICTURE_TYPE_I;
    pPicParams->bPicBackwardPrediction  = vc1->s.pict_type == AV_PICTURE_TYPE_B;


    // Init    Init    Init    Todo
    // iWMV9 - i9IRU - iOHIT - iINSO - iWMVA - 0 - 0 - 0        | Section 3.2.5
    pPicParams->bBidirectionalAveragingMode = (pPicParams->bBidirectionalAveragingMode & 0xE0) |    // init in SetExtraData
                                           ((vc1->lumshift!=0 || vc1->lumscale!=32) ? 0x10 : 0)| // iINSO
                                           ((vc1->profile == PROFILE_ADVANCED)   <<3 );         // iWMVA

    // Section 3.2.20.3
    pPicParams->bPicSpatialResid8   = (vc1->panscanflag   << 7) | (vc1->refdist_flag << 6) |
                                      (vc1->s.loop_filter << 5) | (vc1->fastuvmc     << 4) |
                                      (vc1->extended_mv   << 3) | (vc1->dquant       << 1) |
                                      (vc1->vstransform);

    // Section 3.2.20.4
    pPicParams->bPicOverflowBlocks  = (vc1->quantizer_mode  << 6) | (vc1->multires << 5) |
                                      (vc1->s.resync_marker << 4) | (vc1->rangered << 3) |
                                      (vc1->s.max_b_frames);

    // Section 3.2.20.2
    pPicParams->bPicDeblockConfined = (vc1->postprocflag << 7) | (vc1->broadcast  << 6) |
                                      (vc1->interlace    << 5) | (vc1->tfcntrflag << 4) |
                                      (vc1->finterpflag  << 3) | // (refpic << 2) set in DecodeFrame !
                                      (vc1->psf << 1)          | vc1->extended_dmv;


    pPicParams->bPicStructure = 0;
    if (vc1->s.picture_structure & PICT_TOP_FIELD) {
        pPicParams->bPicStructure |= 0x01;
    }
    if (vc1->s.picture_structure & PICT_BOTTOM_FIELD) {
        pPicParams->bPicStructure |= 0x02;
    }

    pPicParams->bMVprecisionAndChromaRelation = ((vc1->mv_mode == MV_PMODE_1MV_HPEL_BILIN) << 3) |
                                                  (1                                       << 2) |
                                                  (0                                       << 1) |
                                                  (!vc1->s.quarter_sample                      );

    // Cf page 17 : 2 for interlaced, 0 for progressive
    pPicParams->bPicExtrapolation = (!vc1->interlace || vc1->fcm == PROGRESSIVE) ? 1 : 2;

    if (vc1->s.picture_structure == PICT_FRAME) {
        pPicParams->wBitstreamFcodes        = vc1->lumscale;
        pPicParams->wBitstreamPCEelements   = vc1->lumshift;
    } else {
        /* Syntax: (top_field_param << 8) | bottom_field_param */
        pPicParams->wBitstreamFcodes        = (vc1->lumscale << 8) | vc1->lumscale;
        pPicParams->wBitstreamPCEelements   = (vc1->lumshift << 8) | vc1->lumshift;
    }

    if (vc1->profile == PROFILE_ADVANCED) {
        pPicParams->bPicOBMC = (vc1->range_mapy_flag  << 7) |
                               (vc1->range_mapy       << 4) |
                               (vc1->range_mapuv_flag << 3) |
                               (vc1->range_mapuv          );
    }

    // Section 3.2.16
    if (nSliceType) {
        *nSliceType = vc1->s.pict_type;
    }

    // Cf ?7.1.1.25 in VC1 specification, ?3.2.14.3 in DXVA spec
    pPicParams->bRcontrol   = vc1->rnd;

    pPicParams->bPicDeblocked   = ((vc1->profile == PROFILE_ADVANCED && vc1->overlap == 1 &&
                                    pPicParams->bPicBackwardPrediction == 0)                << 6) |
                                  ((vc1->profile != PROFILE_ADVANCED && vc1->rangeredfrm)   << 5) |
                                  (vc1->s.loop_filter                                       << 1);

    if (vc1->profile == PROFILE_ADVANCED && (vc1->rff || vc1->rptfrm)) {
        *b_repeat_pict = TRUE;
    }

    return S_OK;
}

unsigned long FFGetMBNumber(struct AVCodecContext* pAVCtx)
{
    MpegEncContext* s = GetMpegEncContext(pAVCtx);

    return (s != NULL) ? s->mb_num : 0;
}

int FFIsSkipped(struct AVCodecContext* pAVCtx)
{
    VC1Context* vc1 = (VC1Context*) pAVCtx->priv_data;
    return vc1->p_frame_skipped;
}
