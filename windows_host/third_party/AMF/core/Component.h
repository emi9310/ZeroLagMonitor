// AMF SDK - Component.h stub
// Implementation is in amfrt64.dll (AMD drivers)
#pragma once
#include "PropertyStorageEx.h"
#include "Surface.h"
#include "Data.h"

namespace amf {

    // Forward declarations
    class AMFComponent;
    class AMFDataAllocatorCB;
    class AMFComponentOptimizationCallback;
    class AMFCaps;

    //-------------------------------------------------------------------------------------
    // AMFDataAllocatorCB
    //-------------------------------------------------------------------------------------
    class AMF_NO_VTABLE AMFDataAllocatorCB : public AMFInterface {
    public:
        virtual AMF_RESULT AMF_STD_CALL AllocBuffer(AMF_MEMORY_TYPE type,
            amf_size size, AMFBuffer** ppBuffer) = 0;
        virtual AMF_RESULT AMF_STD_CALL AllocSurface(AMF_MEMORY_TYPE type,
            AMF_SURFACE_FORMAT format, amf_int32 width, amf_int32 height,
            AMFSurface** ppSurface) = 0;
    };

    //-------------------------------------------------------------------------------------
    // AMFComponentOptimizationCallback
    //-------------------------------------------------------------------------------------
    class AMF_NO_VTABLE AMFComponentOptimizationCallback {
    public:
        virtual AMF_RESULT AMF_STD_CALL OnComponentOptimizationProgress(
            amf_uint percent) = 0;
    };

    //-------------------------------------------------------------------------------------
    // AMFComponent
    //-------------------------------------------------------------------------------------
    class AMF_NO_VTABLE AMFComponent : public AMFPropertyStorageEx {
    public:
        virtual AMF_RESULT  AMF_STD_CALL Init(AMF_SURFACE_FORMAT format,
                                              amf_int32 width, amf_int32 height) = 0;
        virtual AMF_RESULT  AMF_STD_CALL ReInit(amf_int32 width, amf_int32 height) = 0;
        virtual AMF_RESULT  AMF_STD_CALL Terminate() = 0;
        virtual AMF_RESULT  AMF_STD_CALL Drain() = 0;
        virtual AMF_RESULT  AMF_STD_CALL Flush() = 0;

        virtual AMF_RESULT  AMF_STD_CALL SubmitInput(AMFData* pData) = 0;
        virtual AMF_RESULT  AMF_STD_CALL QueryOutput(AMFData** ppData) = 0;
        virtual AMFContext* AMF_STD_CALL GetContext() = 0;

        virtual AMF_RESULT  AMF_STD_CALL SetOutputDataAllocatorCB(
                                              AMFDataAllocatorCB* callback) = 0;
        virtual AMF_RESULT  AMF_STD_CALL GetCaps(AMFCaps** ppCaps) = 0;
        virtual AMF_RESULT  AMF_STD_CALL Optimize(
                                              AMFComponentOptimizationCallback* pCallback) = 0;
    };

    typedef AMFInterfacePtr_T<AMFComponent> AMFComponentPtr;

} // namespace amf
