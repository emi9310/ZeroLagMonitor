// AMF SDK - Observable.h stub
// Implementation is in amfrt64.dll (AMD drivers)
#pragma once
#include "Interface.h"

namespace amf {
    class AMFObserver {
    public:
        virtual void AMF_STD_CALL OnPropertyChanged(const wchar_t* pName) = 0;
    };

    class AMF_NO_VTABLE AMFObservable {
    public:
        virtual AMF_RESULT AMF_STD_CALL AddObserver   (AMFObserver* pObserver) = 0;
        virtual AMF_RESULT AMF_STD_CALL RemoveObserver(AMFObserver* pObserver) = 0;
    };
} // namespace amf
