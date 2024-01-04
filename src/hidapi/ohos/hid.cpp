/*
  Simple DirectMedia Layer
  Copyright (C) 2022 Valve Corporation

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

// Purpose: A wrapper implementing "HID" API for OHOS
//
//          This layer glues the hidapi API to OHOS's USB and BLE stack.

#include "hid.h"

// Common to stub version and non-stub version of functions
#include <hilog/log.h>
#include <node_api.h>
#include <sys/time.h>
#include <napi/native_api.h>
#include <sys/select.h>

#define TAG "hidapi"

// Have error log always available
#define LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, LOG_DOMAIN, "hid", __VA_ARGS__)

#ifdef DEBUG
#define LOGV(...)  OH_LOG_Print(LOG_APP, LOG_INFO,LOG_DOMAIN, "hid" __VA_ARGS__)
#define LOGD(...)  OH_LOG_Print(LOG_APP, LOG_DEBUG,LOG_DOMAIN, "hid" __VA_ARGS__)
#else
#define LOGV(...)
#define LOGD(...)
#endif

#define OHOSSDKVersion 9
#define SDL_JAVA_PREFIX                             org_libsdl_app
#define CONCAT1(prefix, class, function)            CONCAT2(prefix, class, function)
#define CONCAT2(prefix, class, function)            Java_##prefix##_##class##_##function
#define HID_DEVICE_MANAGER_JAVA_INTERFACE(function) CONCAT1(SDL_JAVA_PREFIX, HIDDeviceManager, function)

#ifndef SDL_HIDAPI_DISABLED

extern "C" {
#include "../SDL_hidapi_c.h"
}

#define hid_close                    PLATFORM_hid_close
#define hid_device                   PLATFORM_hid_device
#define hid_device_                  PLATFORM_hid_device_
#define hid_enumerate                PLATFORM_hid_enumerate
#define hid_error                    PLATFORM_hid_error
#define hid_exit                     PLATFORM_hid_exit
#define hid_free_enumeration         PLATFORM_hid_free_enumeration
#define hid_get_device_info          PLATFORM_hid_get_device_info
#define hid_get_feature_report       PLATFORM_hid_get_feature_report
#define hid_get_indexed_string       PLATFORM_hid_get_indexed_string
#define hid_get_input_report         PLATFORM_hid_get_input_report
#define hid_get_manufacturer_string  PLATFORM_hid_get_manufacturer_string
#define hid_get_product_string       PLATFORM_hid_get_product_string
#define hid_get_report_descriptor    PLATFORM_hid_get_report_descriptor
#define hid_get_serial_number_string PLATFORM_hid_get_serial_number_string
#define hid_init                     PLATFORM_hid_init
#define hid_open_path                PLATFORM_hid_open_path
#define hid_open                     PLATFORM_hid_open
#define hid_read                     PLATFORM_hid_read
#define hid_read_timeout             PLATFORM_hid_read_timeout
#define hid_send_feature_report      PLATFORM_hid_send_feature_report
#define hid_set_nonblocking          PLATFORM_hid_set_nonblocking
#define hid_version                  PLATFORM_hid_version
#define hid_version_str              PLATFORM_hid_version_str
#define hid_write                    PLATFORM_hid_write

#include <errno.h> // For ETIMEDOUT and ECONNRESET
#include <pthread.h>
#include <stdlib.h> // For malloc() and free()

#include "../hidapi/hidapi.h"

typedef uint32_t uint32;
typedef uint64_t uint64;

struct hid_device_
{
    int m_nId;
    int m_nDeviceRefCount;
};
static napi_env env = nullptr;
static pthread_key_t g_ThreadKey;

template <class T>
class hid_device_ref
{
  public:
    hid_device_ref(T *pObject = nullptr) : m_pObject(nullptr)
    {
        SetObject(pObject);
    }

    hid_device_ref(const hid_device_ref &rhs) : m_pObject(nullptr)
    {
        SetObject(rhs.GetObject());
    }

    ~hid_device_ref()
    {
        SetObject(nullptr);
    }

    void SetObject(T *pObject)
    {
        if (m_pObject && m_pObject->DecrementRefCount() == 0) {
            delete m_pObject;
        }

        m_pObject = pObject;

        if (m_pObject) {
            m_pObject->IncrementRefCount();
        }
    }

    hid_device_ref &operator=(T *pObject)
    {
        SetObject(pObject);
        return *this;
    }

    hid_device_ref &operator=(const hid_device_ref &rhs)
    {
        SetObject(rhs.GetObject());
        return *this;
    }

    T *GetObject() const
    {
        return m_pObject;
    }

    T *operator->() const
    {
        return m_pObject;
    }

    operator bool() const
    {
        return (m_pObject != nullptr);
    }

  private:
    T *m_pObject;
};

class hid_mutex_guard
{
  public:
    hid_mutex_guard(pthread_mutex_t *pMutex) : m_pMutex(pMutex)
    {
        pthread_mutex_lock(m_pMutex);
    }
    ~hid_mutex_guard()
    {
        pthread_mutex_unlock(m_pMutex);
    }

  private:
    pthread_mutex_t *m_pMutex;
};

class hid_buffer
{
  public:
    hid_buffer() : m_pData(nullptr), m_nSize(0), m_nAllocated(0)
    {
    }

    hid_buffer(const uint8_t *pData, size_t nSize) : m_pData(nullptr), m_nSize(0), m_nAllocated(0)
    {
        assign(pData, nSize);
    }

    ~hid_buffer()
    {
        delete[] m_pData;
    }

    void assign(const uint8_t *pData, size_t nSize)
    {
        if (nSize > m_nAllocated) {
            delete[] m_pData;
            m_pData = new uint8_t[nSize];
            m_nAllocated = nSize;
        }

        m_nSize = nSize;
        SDL_memcpy(m_pData, pData, nSize);
    }

    void clear()
    {
        m_nSize = 0;
    }

    size_t size() const
    {
        return m_nSize;
    }

    const uint8_t *data() const
    {
        return m_pData;
    }

  private:
    uint8_t *m_pData;
    size_t m_nSize;
    size_t m_nAllocated;
};

class hid_buffer_pool
{
  public:
    hid_buffer_pool() : m_nSize(0), m_pHead(nullptr), m_pTail(nullptr), m_pFree(nullptr)
    {
    }

    ~hid_buffer_pool()
    {
        clear();

        while (m_pFree) {
            hid_buffer_entry *pEntry = m_pFree;
            m_pFree = m_pFree->m_pNext;
            delete pEntry;
        }
    }

    size_t size() const { return m_nSize; }

    const hid_buffer &front() const { return m_pHead->m_buffer; }

    void pop_front()
    {
        hid_buffer_entry *pEntry = m_pHead;
        if (pEntry) {
            m_pHead = pEntry->m_pNext;
            if (!m_pHead) {
                m_pTail = nullptr;
            }
            pEntry->m_pNext = m_pFree;
            m_pFree = pEntry;
            --m_nSize;
        }
    }

    void emplace_back(const uint8_t *pData, size_t nSize)
    {
        hid_buffer_entry *pEntry;

        if (m_pFree) {
            pEntry = m_pFree;
            m_pFree = m_pFree->m_pNext;
        } else {
            pEntry = new hid_buffer_entry;
        }
        pEntry->m_pNext = nullptr;

        if (m_pTail) {
            m_pTail->m_pNext = pEntry;
        } else {
            m_pHead = pEntry;
        }
        m_pTail = pEntry;

        pEntry->m_buffer.assign(pData, nSize);
        ++m_nSize;
    }

    void clear()
    {
        while (size() > 0) {
            pop_front();
        }
    }

  private:
    struct hid_buffer_entry
    {
        hid_buffer m_buffer;
        hid_buffer_entry *m_pNext;
    };

    size_t m_nSize;
    hid_buffer_entry *m_pHead;
    hid_buffer_entry *m_pTail;
    hid_buffer_entry *m_pFree;
};

static char *CreateStringFromJString(napi_env env, const napi_value &sString)
{
    return nullptr;
}

static wchar_t *CreateWStringFromJString(napi_env env, const napi_value &sString)
{
    return nullptr;
}

static wchar_t *CreateWStringFromWString(const wchar_t *pwSrc)
{
    size_t nLength = SDL_wcslen(pwSrc);
    wchar_t *pwString = (wchar_t *)malloc((nLength + 1) * sizeof(wchar_t));
    SDL_memcpy(pwString, pwSrc, nLength * sizeof(wchar_t));
    pwString[nLength] = '\0';
    return pwString;
}

static hid_device_info *CopyHIDDeviceInfo(const hid_device_info *pInfo)
{
    hid_device_info *pCopy = new hid_device_info;
    *pCopy = *pInfo;
    pCopy->path = SDL_strdup(pInfo->path);
    pCopy->product_string = CreateWStringFromWString(pInfo->product_string);
    pCopy->manufacturer_string = CreateWStringFromWString(pInfo->manufacturer_string);
    pCopy->serial_number = CreateWStringFromWString(pInfo->serial_number);
    return pCopy;
}

static void FreeHIDDeviceInfo(hid_device_info *pInfo)
{
    free(pInfo->path);
    free(pInfo->serial_number);
    free(pInfo->manufacturer_string);
    free(pInfo->product_string);
    delete pInfo;
}

static napi_ref g_HIDDeviceManagerCallbackClass;
static napi_value g_HIDDeviceManagerCallbackHandler;
static napi_ref g_midHIDDeviceManagerInitialize;
static napi_ref g_midHIDDeviceManagerOpen;
static napi_ref g_midHIDDeviceManagerWriteReport;
static napi_ref g_midHIDDeviceManagerReadReport;
static napi_ref g_midHIDDeviceManagerClose;

static bool g_initialized = false;

static uint64_t get_timespec_ms(const struct timespec &ts)
{
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void ExceptionCheck(napi_env env, const char *pszClassName, const char *pszMethodName)
{
    napi_value errorInfo;
    napi_get_and_clear_last_exception(env, &errorInfo);
    LOGE( "%{public}s%{public}s{public}%s threw an exception: %{public}s",
    			pszClassName ? pszClassName : "",
    			pszClassName ? "::" : "",
    			pszMethodName, "");
}

class CHIDDevice
{
  public:
    CHIDDevice(int nDeviceID, hid_device_info *pInfo)
    {
        m_nId = nDeviceID;
        m_pInfo = pInfo;

        // The Bluetooth Steam Controller needs special handling
        const int VALVE_USB_VID = 0x28DE;
        const int D0G_BLE2_PID = 0x1106;
        if (pInfo->vendor_id == VALVE_USB_VID && pInfo->product_id == D0G_BLE2_PID) {
            m_bIsBLESteamController = true;
        }
    }

    ~CHIDDevice()
    {
        FreeHIDDeviceInfo(m_pInfo);

        // Note that we don't delete m_pDevice, as the app may still have a reference to it
    }

    int IncrementRefCount()
    {
        int nValue;
        pthread_mutex_lock(&m_refCountLock);
        nValue = ++m_nRefCount;
        pthread_mutex_unlock(&m_refCountLock);
        return nValue;
    }

    int DecrementRefCount()
    {
        int nValue;
        pthread_mutex_lock(&m_refCountLock);
        nValue = --m_nRefCount;
        pthread_mutex_unlock(&m_refCountLock);
        return nValue;
    }

    int GetId()
    {
        return m_nId;
    }

    hid_device_info *GetDeviceInfo()
    {
        return m_pInfo;
    }

    hid_device *GetDevice()
    {
        return m_pDevice;
    }

    void ExceptionCheck(napi_env env, const char *pszMethodName)
    {
        ::ExceptionCheck(env, "CHIDDevice", pszMethodName);
    }

    bool BOpen()
    {

        m_bIsWaitingForOpen = false;
        napi_value jsCallback;
        napi_value params[1];
        napi_create_int32(env, m_nId, &params[0]);
        napi_get_reference_value(env, g_midHIDDeviceManagerOpen, &jsCallback);
        napi_value result;
        napi_call_function(env, nullptr, jsCallback, 1, params, &result);
        napi_get_value_bool(env, result, &m_bOpenResult);

        ExceptionCheck( env, "BOpen" );

        LOGD("Creating device %d (%p), refCount = 1\n", m_pDevice->m_nId, m_pDevice);

		if ( m_bIsWaitingForOpen )
		{
			hid_mutex_guard cvl( &m_cvLock );

			const int OPEN_TIMEOUT_SECONDS = 60;
			struct timespec ts, endtime;
			clock_gettime( CLOCK_REALTIME, &ts );
			endtime = ts;
			endtime.tv_sec += OPEN_TIMEOUT_SECONDS;
			do
			{
				if ( pthread_cond_timedwait( &m_cv, &m_cvLock, &endtime ) != 0 )
				{
					break;
				}
			}
			while ( m_bIsWaitingForOpen && get_timespec_ms( ts ) < get_timespec_ms( endtime ) );
		}

		if ( !m_bOpenResult )
		{
			if ( m_bIsWaitingForOpen )
			{
				LOGV( "Device open failed - timed out waiting for device permission" );
			}
			else
			{
				LOGV( "Device open failed" );
			}
			return false;
		}

		m_pDevice = new hid_device;
		m_pDevice->m_nId = m_nId;
		m_pDevice->m_nDeviceRefCount = 1;
		LOGD("Creating device %d (%p), refCount = 1\n", m_pDevice->m_nId, m_pDevice);
        return true;
    }

    void SetOpenPending()
    {
        m_bIsWaitingForOpen = true;
    }

    void SetOpenResult(bool bResult)
    {
        if (m_bIsWaitingForOpen) {
            m_bOpenResult = bResult;
            m_bIsWaitingForOpen = false;
            pthread_cond_signal(&m_cv);
        }
    }

    void ProcessInput(const uint8_t *pBuf, size_t nBufSize)
    {
        hid_mutex_guard l(&m_dataLock);

        size_t MAX_REPORT_QUEUE_SIZE = 16;
        if (m_vecData.size() >= MAX_REPORT_QUEUE_SIZE) {
            m_vecData.pop_front();
        }
        m_vecData.emplace_back(pBuf, nBufSize);
    }

    int GetInput(unsigned char *data, size_t length)
    {
        hid_mutex_guard l(&m_dataLock);

        if (m_vecData.size() == 0) {
            //			LOGV( "hid_read_timeout no data available" );
            return 0;
        }

        const hid_buffer &buffer = m_vecData.front();
        size_t nDataLen = buffer.size() > length ? length : buffer.size();
        if (m_bIsBLESteamController) {
            data[0] = 0x03;
            SDL_memcpy(data + 1, buffer.data(), nDataLen);
            ++nDataLen;
        } else {
            SDL_memcpy(data, buffer.data(), nDataLen);
        }
        m_vecData.pop_front();

        //		LOGV("Read %u bytes", nDataLen);
        //		LOGV("%02x %02x %02x %02x %02x %02x %02x %02x ....",
        //			 data[0], data[1], data[2], data[3],
        //			 data[4], data[5], data[6], data[7]);

        return (int)nDataLen;
    }

    int WriteReport(const unsigned char *pData, size_t nDataLen, bool bFeature)
    {
        // Make sure thread is attached to JVM/env
        LOGV("WriteReport without callback handler");
        return 0;
    }

    void ProcessReportResponse(const uint8_t *pBuf, size_t nBufSize)
    {
        hid_mutex_guard cvl(&m_cvLock);
        if (m_bIsWaitingForReportResponse) {
            m_reportResponse.assign(pBuf, nBufSize);

            m_bIsWaitingForReportResponse = false;
            m_nReportResponseError = 0;
            pthread_cond_signal(&m_cv);
        }
    }

    int ReadReport(unsigned char *pData, size_t nDataLen, bool bFeature)
    {
        return 0;
    }

    void Close(bool bDeleteDevice)
    {
    }

  private:
    pthread_mutex_t m_refCountLock = PTHREAD_MUTEX_INITIALIZER;
    int m_nRefCount = 0;
    int m_nId = 0;
    hid_device_info *m_pInfo = nullptr;
    hid_device *m_pDevice = nullptr;
    bool m_bIsBLESteamController = false;

    pthread_mutex_t m_dataLock = PTHREAD_MUTEX_INITIALIZER; // This lock has to be held to access m_vecData
    hid_buffer_pool m_vecData;

    // For handling get_feature_report
    pthread_mutex_t m_cvLock = PTHREAD_MUTEX_INITIALIZER; // This lock has to be held to access any variables below
    pthread_cond_t m_cv = PTHREAD_COND_INITIALIZER;
    bool m_bIsWaitingForOpen = false;
    bool m_bOpenResult = false;
    bool m_bIsWaitingForReportResponse = false;
    int m_nReportResponseError = 0;
    hid_buffer m_reportResponse;

  public:
    hid_device_ref<CHIDDevice> next;
};

class CHIDDevice;
static pthread_mutex_t g_DevicesMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_DevicesRefCountMutex = PTHREAD_MUTEX_INITIALIZER;
static hid_device_ref<CHIDDevice> g_Devices;

static hid_device_ref<CHIDDevice> FindDevice(int nDeviceId)
{
    hid_device_ref<CHIDDevice> pDevice;

    hid_mutex_guard l(&g_DevicesMutex);
    for (pDevice = g_Devices; pDevice; pDevice = pDevice->next) {
        if (pDevice->GetId() == nDeviceId) {
            break;
        }
    }
    return pDevice;
}

static void ThreadDestroyed(void *value)
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern "C" {


int hid_init(void)
{
    if ( !g_initialized && g_midHIDDeviceManagerInitialize )
    	{
    		// HIDAPI doesn't work well with OHOS < 8
    		if (OHOSSDKVersion >= 8) {

    			// Bluetooth is currently only used for Steam Controllers, so check that hint
    			// before initializing Bluetooth, which will prompt the user for permission.
    			bool init_usb = true;
    			bool init_bluetooth = false;
    			if (SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_STEAM, SDL_FALSE)) {
    				init_bluetooth = true;
    			}
                napi_value jsCallback;
                napi_value params[2];
                napi_get_boolean(env, init_usb, &params[0]);
                napi_get_boolean(env, init_bluetooth, &params[1]);
                napi_get_reference_value(env, g_midHIDDeviceManagerInitialize, &jsCallback);
                napi_value result;
                napi_call_function(env, nullptr, jsCallback, 2, params, &result);
    			ExceptionCheck( env, NULL, "hid_init" );
    		}
    		g_initialized = true;	// Regardless of result, so it's only called once
    	}
    return 0;
}

struct hid_device_info HID_API_EXPORT *HID_API_CALL hid_enumerate(unsigned short vendor_id, unsigned short product_id)
{
    struct hid_device_info *root = NULL;

    hid_mutex_guard l(&g_DevicesMutex);
    for (hid_device_ref<CHIDDevice> pDevice = g_Devices; pDevice; pDevice = pDevice->next) {
        const hid_device_info *info = pDevice->GetDeviceInfo();

        /* See if there are any devices we should skip in enumeration */
        if (SDL_HIDAPI_ShouldIgnoreDevice(HID_API_BUS_UNKNOWN, info->vendor_id, info->product_id, 0, 0)) {
            continue;
        }

        if ((vendor_id == 0x0 || info->vendor_id == vendor_id) &&
            (product_id == 0x0 || info->product_id == product_id)) {
            hid_device_info *dev = CopyHIDDeviceInfo(info);
            dev->next = root;
            root = dev;
        }
    }
    return root;
}

void HID_API_EXPORT HID_API_CALL hid_free_enumeration(struct hid_device_info *devs)
{
    while (devs) {
        struct hid_device_info *next = devs->next;
        FreeHIDDeviceInfo(devs);
        devs = next;
    }
}

HID_API_EXPORT hid_device *HID_API_CALL hid_open(unsigned short vendor_id, unsigned short product_id, const wchar_t *serial_number)
{
    // TODO: Implement
    return NULL;
}

HID_API_EXPORT hid_device *HID_API_CALL hid_open_path(const char *path)
{
    LOGV("hid_open_path( %s )", path);

    hid_device_ref<CHIDDevice> pDevice;
    {
        hid_mutex_guard r(&g_DevicesRefCountMutex);
        hid_mutex_guard l(&g_DevicesMutex);
        for (hid_device_ref<CHIDDevice> pCurr = g_Devices; pCurr; pCurr = pCurr->next) {
            if (SDL_strcmp(pCurr->GetDeviceInfo()->path, path) == 0) {
                hid_device *pValue = pCurr->GetDevice();
                if (pValue) {
                    ++pValue->m_nDeviceRefCount;
                    LOGD("Incrementing device %d (%p), refCount = %d\n", pValue->m_nId, pValue, pValue->m_nDeviceRefCount);
                    return pValue;
                }

                // Hold a shared pointer to the controller for the duration
                pDevice = pCurr;
                break;
            }
        }
    }
    if (pDevice && pDevice->BOpen()) {
        return pDevice->GetDevice();
    }
    return NULL;
}

int HID_API_EXPORT HID_API_CALL hid_write(hid_device *device, const unsigned char *data, size_t length)
{
    if (device) {
        LOGV("hid_write id=%d length=%u", device->m_nId, length);
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            return pDevice->WriteReport(data, length, false);
        }
    }
    return -1; // Controller was disconnected
}

static uint32_t getms()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32_t)(now.tv_sec * 1000 + now.tv_usec / 1000);
}

static void delayms(uint32_t ms)
{
    int was_error;

    struct timespec elapsed, tv;

    /* Set the timeout interval */
    elapsed.tv_sec = ms / 1000;
    elapsed.tv_nsec = (ms % 1000) * 1000000;
    do {
        errno = 0;

        tv.tv_sec = elapsed.tv_sec;
        tv.tv_nsec = elapsed.tv_nsec;
        was_error = nanosleep(&tv, &elapsed);
    } while (was_error && (errno == EINTR));
}

int HID_API_EXPORT HID_API_CALL hid_read_timeout(hid_device *device, unsigned char *data, size_t length, int milliseconds)
{
    if (device) {
        //		LOGV( "hid_read_timeout id=%d length=%u timeout=%d", device->m_nId, length, milliseconds );
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            int nResult = pDevice->GetInput(data, length);
            if (nResult == 0 && milliseconds > 0) {
                uint32_t start = getms();
                do {
                    delayms(1);
                    nResult = pDevice->GetInput(data, length);
                } while (nResult == 0 && (getms() - start) < milliseconds);
            }
            return nResult;
        }
        LOGV("controller was disconnected");
    }
    return -1; // Controller was disconnected
}

// TODO: Implement blocking
int HID_API_EXPORT HID_API_CALL hid_read(hid_device *device, unsigned char *data, size_t length)
{
    LOGV("hid_read id=%d length=%u", device->m_nId, length);
    return hid_read_timeout(device, data, length, 0);
}

// TODO: Implement?
int HID_API_EXPORT HID_API_CALL hid_set_nonblocking(hid_device *device, int nonblock)
{
    return -1;
}

int HID_API_EXPORT HID_API_CALL hid_send_feature_report(hid_device *device, const unsigned char *data, size_t length)
{
    if (device) {
        LOGV("hid_send_feature_report id=%d length=%u", device->m_nId, length);
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            return pDevice->WriteReport(data, length, true);
        }
    }
    return -1; // Controller was disconnected
}

// Synchronous operation. Will block until completed.
int HID_API_EXPORT HID_API_CALL hid_get_feature_report(hid_device *device, unsigned char *data, size_t length)
{
    if (device) {
        LOGV("hid_get_feature_report id=%d length=%u", device->m_nId, length);
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            return pDevice->ReadReport(data, length, true);
        }
    }
    return -1; // Controller was disconnected
}

// Synchronous operation. Will block until completed.
int HID_API_EXPORT HID_API_CALL hid_get_input_report(hid_device *device, unsigned char *data, size_t length)
{
    if (device) {
        LOGV("hid_get_input_report id=%d length=%u", device->m_nId, length);
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            return pDevice->ReadReport(data, length, false);
        }
    }
    return -1; // Controller was disconnected
}

void HID_API_EXPORT HID_API_CALL hid_close(hid_device *device)
{
    if (device) {
        LOGV("hid_close id=%d", device->m_nId);
        hid_mutex_guard r(&g_DevicesRefCountMutex);
        LOGD("Decrementing device %d (%p), refCount = %d\n", device->m_nId, device, device->m_nDeviceRefCount - 1);
        if (--device->m_nDeviceRefCount == 0) {
            hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
            if (pDevice) {
                pDevice->Close(true);
            } else {
                delete device;
            }
            LOGD("Deleted device %p\n", device);
        }
    }
}

int HID_API_EXPORT_CALL hid_get_manufacturer_string(hid_device *device, wchar_t *string, size_t maxlen)
{
    if (device) {
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            wcsncpy(string, pDevice->GetDeviceInfo()->manufacturer_string, maxlen);
            return 0;
        }
    }
    return -1;
}

int HID_API_EXPORT_CALL hid_get_product_string(hid_device *device, wchar_t *string, size_t maxlen)
{
    if (device) {
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            wcsncpy(string, pDevice->GetDeviceInfo()->product_string, maxlen);
            return 0;
        }
    }
    return -1;
}

int HID_API_EXPORT_CALL hid_get_serial_number_string(hid_device *device, wchar_t *string, size_t maxlen)
{
    if (device) {
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            wcsncpy(string, pDevice->GetDeviceInfo()->serial_number, maxlen);
            return 0;
        }
    }
    return -1;
}

int HID_API_EXPORT_CALL hid_get_indexed_string(hid_device *device, int string_index, wchar_t *string, size_t maxlen)
{
    return -1;
}

struct hid_device_info *hid_get_device_info(hid_device *device)
{
    if (device) {
        hid_device_ref<CHIDDevice> pDevice = FindDevice(device->m_nId);
        if (pDevice) {
            return pDevice->GetDeviceInfo();
        }
    }
    return NULL;
}

int hid_get_report_descriptor(hid_device *device, unsigned char *buf, size_t buf_size)
{
    // Not implemented
    return -1;
}

HID_API_EXPORT const wchar_t *HID_API_CALL hid_error(hid_device *device)
{
    return NULL;
}

int hid_exit(void)
{
    return 0;
}
}
#else

#endif /* SDL_HIDAPI_DISABLED */
