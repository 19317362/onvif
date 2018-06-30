/************************************************************************
**
** ���ߣ�����
** ���ڣ�2018-06-28
** ������ONVIF��װ����
**
************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "wsseapi.h"
#include "onvif_comm.h"
#include "onvif_dump.h"

void soap_perror(struct soap *soap, const char *str)
{
    if (NULL == str) {
        SOAP_DBGERR("[soap] error: %d, %s, %s\n", soap->error, *soap_faultcode(soap), *soap_faultstring(soap));
    } else {
        SOAP_DBGERR("[soap] %s error: %d, %s, %s\n", str, soap->error, *soap_faultcode(soap), *soap_faultstring(soap));
    }
    return;
}

void* ONVIF_soap_malloc(struct soap *soap, unsigned int n)
{
    void *p = NULL;

    if (n > 0) {
        p = soap_malloc(soap, n);
        SOAP_ASSERT(NULL != p);
        memset(p, 0x00 ,n);
    }
    return p;
}

struct soap *ONVIF_soap_new(int timeout)
{
    struct soap *soap = NULL;                                                   // soap��������

    SOAP_ASSERT(NULL != (soap = soap_new()));

    soap_set_namespaces(soap, namespaces);                                      // ����soap��namespaces
    soap->recv_timeout    = timeout;                                            // ���ó�ʱ������ָ��ʱ��û�����ݾ��˳���
    soap->send_timeout    = timeout;
    soap->connect_timeout = timeout;

#if defined(__linux__) || defined(__linux)                                      // �ο�https://www.genivia.com/dev.html#client-c���޸ģ�
    soap->socket_flags = MSG_NOSIGNAL;                                          // To prevent connection reset errors
#endif

    soap_set_mode(soap, SOAP_C_UTFSTRING);                                      // ����ΪUTF-8���룬�����������OSD������

    return soap;
}

void ONVIF_soap_delete(struct soap *soap)
{
    soap_destroy(soap);                                                         // remove deserialized class instances (C++ only)
    soap_end(soap);                                                             // Clean up deserialized data (except class instances) and temporary data
    soap_done(soap);                                                            // Reset, close communications, and remove callbacks
    soap_free(soap);                                                            // Reset and deallocate the context created with soap_new or soap_copy
}

/************************************************************************
**������ONVIF_SetAuthInfo
**���ܣ�������֤��Ϣ
**������
        [in] soap     - soap��������
        [in] username - �û���
        [in] password - ����
**���أ�
        0�����ɹ�����0����ʧ��
**��ע��
************************************************************************/
int ONVIF_SetAuthInfo(struct soap *soap, const char *username, const char *password)
{
    int result = 0;

    SOAP_ASSERT(NULL != username);
    SOAP_ASSERT(NULL != password);

    result = soap_wsse_add_UsernameTokenDigest(soap, NULL, username, password);
    SOAP_CHECK_ERROR(result, soap, "add_UsernameTokenDigest");

EXIT:

    return result;
}

/************************************************************************
**������ONVIF_init_header
**���ܣ���ʼ��soap������Ϣͷ
**������
        [in] soap - soap��������
**���أ���
**��ע��
    1). �ڱ������ڲ�ͨ��ONVIF_soap_malloc������ڴ棬����ONVIF_soap_delete�б��ͷ�
************************************************************************/
void ONVIF_init_header(struct soap *soap)
{
    struct SOAP_ENV__Header *header = NULL;

    SOAP_ASSERT(NULL != soap);

    header = (struct SOAP_ENV__Header *)ONVIF_soap_malloc(soap, sizeof(struct SOAP_ENV__Header));
    soap_default_SOAP_ENV__Header(soap, header);
    header->wsa__MessageID = (char*)soap_wsa_rand_uuid(soap);
    header->wsa__To        = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_TO) + 1);
    header->wsa__Action    = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_ACTION) + 1);
    strcpy(header->wsa__To, SOAP_TO);
    strcpy(header->wsa__Action, SOAP_ACTION);
    soap->header = header;

    return;
}

/************************************************************************
**������ONVIF_init_ProbeType
**���ܣ���ʼ��̽���豸�ķ�Χ������
**������
        [in]  soap  - soap��������
        [out] probe - ���Ҫ̽����豸��Χ������
**���أ�
        0����̽�⵽����0����δ̽�⵽
**��ע��
    1). �ڱ������ڲ�ͨ��ONVIF_soap_malloc������ڴ棬����ONVIF_soap_delete�б��ͷ�
************************************************************************/
void ONVIF_init_ProbeType(struct soap *soap, struct wsdd__ProbeType *probe)
{
    struct wsdd__ScopesType *scope = NULL;                                      // �����������������Web����

    SOAP_ASSERT(NULL != soap);
    SOAP_ASSERT(NULL != probe);

    scope = (struct wsdd__ScopesType *)ONVIF_soap_malloc(soap, sizeof(struct wsdd__ScopesType));
    soap_default_wsdd__ScopesType(soap, scope);                                 // ����Ѱ���豸�ķ�Χ
    scope->__item = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_ITEM) + 1);
    strcpy(scope->__item, SOAP_ITEM);

    memset(probe, 0x00, sizeof(struct wsdd__ProbeType));
    soap_default_wsdd__ProbeType(soap, probe);
    probe->Scopes = scope;
    probe->Types  = (char*)ONVIF_soap_malloc(soap, strlen(SOAP_TYPES) + 1);     // ����Ѱ���豸������
    strcpy(probe->Types, SOAP_TYPES);

    return;
}

void ONVIF_DetectDevice(void (*cb)(char *DeviceXAddr))
{
    int i;
    int result = 0;
    unsigned int count = 0;                                                     // ���������豸����
    struct soap *soap = NULL;                                                   // soap��������
    struct wsdd__ProbeType      req;                                            // ���ڷ���Probe��Ϣ
    struct __wsdd__ProbeMatches rep;                                            // ���ڽ���ProbeӦ��
    struct wsdd__ProbeMatchType *probeMatch;

    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    ONVIF_init_header(soap);                                                    // ������Ϣͷ����
    ONVIF_init_ProbeType(soap, &req);                                           // ����Ѱ�ҵ��豸�ķ�Χ������
    result = soap_send___wsdd__Probe(soap, SOAP_MCAST_ADDR, NULL, &req);        // ���鲥��ַ�㲥Probe��Ϣ
    while (SOAP_OK == result)                                                   // ��ʼѭ�������豸���͹�������Ϣ
    {
        memset(&rep, 0x00, sizeof(rep));
        result = soap_recv___wsdd__ProbeMatches(soap, &rep);
        if (SOAP_OK == result) {
            if (soap->error) {
                soap_perror(soap, "ProbeMatches");
            } else {                                                            // �ɹ����յ��豸��Ӧ����Ϣ
                dump__wsdd__ProbeMatches(&rep);

                if (NULL != rep.wsdd__ProbeMatches) {
                    count += rep.wsdd__ProbeMatches->__sizeProbeMatch;
                    for(i = 0; i < rep.wsdd__ProbeMatches->__sizeProbeMatch; i++) {
                        probeMatch = rep.wsdd__ProbeMatches->ProbeMatch + i;
                        if (NULL != cb) {
                            cb(probeMatch->XAddrs);                             // ʹ���豸�����ִַ�к����ص�
                        }
                    }
                }
            }
        } else if (soap->error) {
            break;
        }
    }

    SOAP_DBGLOG("\ndetect end! It has detected %d devices!\n", count);

    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return ;
}

/************************************************************************
**������ONVIF_GetProfiles
**���ܣ���ȡ�豸������Ƶ����������Ϣ
**������
        [in] MediaXAddr - ý������ַ
        [out] profiles  - ���ص��豸����Ƶ����������Ϣ�б�������������ʹ��free�ͷŸû���
**���أ�
        �����豸��֧�ֵ�����������ͨ������/������������ʹprofiles�б����
**��ע��
        1). ע�⣺һ���������������������԰�����Ƶ����Ƶ���ݣ�Ҳ���Խ���������Ƶ���ݡ�
************************************************************************/
int ONVIF_GetProfiles(const char *MediaXAddr, struct tagProfile **profiles)
{
    int i = 0;
    int result = 0;
    struct soap *soap = NULL;
    struct _trt__GetProfiles            req;
    struct _trt__GetProfilesResponse    rep;

    SOAP_ASSERT(NULL != MediaXAddr);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

    memset(&req, 0x00, sizeof(req));
    memset(&rep, 0x00, sizeof(rep));
    result = soap_call___trt__GetProfiles(soap, MediaXAddr, NULL, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "GetProfiles");

    dump_trt__GetProfilesResponse(&rep);

    if (rep.__sizeProfiles > 0) {                                               // ���仺��
        (*profiles) = (struct tagProfile *)malloc(rep.__sizeProfiles * sizeof(struct tagProfile));
        SOAP_ASSERT(NULL != (*profiles));
        memset((*profiles), 0x00, rep.__sizeProfiles * sizeof(struct tagProfile));
    }

    for(i = 0; i < rep.__sizeProfiles; i++) {                                   // ��ȡ���������ļ���Ϣ�����������ĵģ�
        struct tt__Profile *ttProfile = &rep.Profiles[i];
        struct tagProfile *plst = &(*profiles)[i];

        if (NULL != ttProfile->token) {                                         // �����ļ�Token
            strncpy(plst->token, ttProfile->token, sizeof(plst->token) - 1);
        }

        if (NULL != ttProfile->VideoEncoderConfiguration) {                     // ��Ƶ������������Ϣ
            if (NULL != ttProfile->VideoEncoderConfiguration->token) {          // ��Ƶ������Token
                strncpy(plst->venc.token, ttProfile->VideoEncoderConfiguration->token, sizeof(plst->venc.token) - 1);
            }
            if (NULL != ttProfile->VideoEncoderConfiguration->Resolution) {     // ��Ƶ�������ֱ���
                plst->venc.Width  = ttProfile->VideoEncoderConfiguration->Resolution->Width;
                plst->venc.Height = ttProfile->VideoEncoderConfiguration->Resolution->Height;
            }
        }
    }

EXIT:

    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return rep.__sizeProfiles;
}

/************************************************************************
**������ONVIF_GetCapabilities
**���ܣ���ȡ�豸������Ϣ
**������
        [in] DeviceXAddr - �豸�����ַ
        [out] capa       - �����豸������Ϣ��Ϣ
**���أ�
        0�����ɹ�����0����ʧ��
**��ע��
    1). ��������Ҫ�Ĳ���֮һ��ý������ַ
************************************************************************/
int ONVIF_GetCapabilities(const char *DeviceXAddr, struct tagCapabilities *capa)
{
    int result = 0;
    struct soap *soap = NULL;
    struct _tds__GetCapabilities            req;
    struct _tds__GetCapabilitiesResponse    rep;

    SOAP_ASSERT(NULL != DeviceXAddr);
    SOAP_ASSERT(NULL != capa);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

    memset(&req, 0x00, sizeof(req));
    memset(&rep, 0x00, sizeof(rep));
    result = soap_call___tds__GetCapabilities(soap, DeviceXAddr, NULL, &req, &rep);
    SOAP_CHECK_ERROR(result, soap, "GetCapabilities");

    dump_tds__GetCapabilitiesResponse(&rep);

    memset(capa, 0x00, sizeof(struct tagCapabilities));
    if (NULL != rep.Capabilities) {
        if (NULL != rep.Capabilities->Media) {
            if (NULL != rep.Capabilities->Media->XAddr) {
                strncpy(capa->MediaXAddr, rep.Capabilities->Media->XAddr, sizeof(capa->MediaXAddr) - 1);
            }
        }
        if (NULL != rep.Capabilities->Events) {
            if (NULL != rep.Capabilities->Events->XAddr) {
                strncpy(capa->EventXAddr, rep.Capabilities->Events->XAddr, sizeof(capa->EventXAddr) - 1);
            }
        }
        if (NULL != rep.Capabilities->PTZ) {
            if (NULL != rep.Capabilities->PTZ->XAddr) {
                strncpy(capa->PTZXAddr, rep.Capabilities->PTZ->XAddr, sizeof(capa->PTZXAddr) - 1);
            }
        }
    }

EXIT:

    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}

/************************************************************************
**������ONVIF_PTZ_ContinuousMove
**���ܣ�ʹ�豸�������õ�PTZ�ٶȳ����ƶ�
**������
        [in] profile    - MediaProfile������ 
        [in] endpoint   - �豸�����ַ
        [in] speed_Pan  - ˮƽ�ƶ��ٶ�, ȡֵ��Χ(-100, +100)
        [in] speed_Tilt - ��б���ƶ��ٶ�, ȡֵ��Χ(-100, +100)
        [in] speed_Zoom - ������ƶ��ٶ�, ȡֵ��Χ(-100, +100) 
**���أ�
        0�����ɹ�����0����ʧ��
**��ע��
		1) ����ֵԽ�� ����Ŀ��Խ���� ����ֵԽС�� ����Ŀ��ԽԶ
		2) �豸���ƶ��ٶ��뽹���йأ� ����Խ���ٶ�Խ��	
		3) �ٶȵ����������� PAN ��ֵ����˳ʱ��ת������ֵ������ʱ��ת��, 0ֵ�����ƶ�
		                    Tilt ��ֵ���������ƶ��� ��ֵ���������ƶ�, 0ֵ�����ƶ�
							Zoom ֵԽ�������Խ�� 0ֵ�����ƶ�
************************************************************************/

int ONVIF_PTZ_ContinuousMove(char *profile, char *endpoint, int speed_Pan, int speed_Tilt, int speed_Zoom)
{
	struct _tptz__ContinuousMove 	 			tptz__ContinuousMove;
	struct _tptz__ContinuousMoveResponse 		tptz__ContinuousMoveresponse;
    struct soap *soap = NULL;
	int result = 0;

    SOAP_ASSERT(NULL != profile);
    SOAP_ASSERT(NULL != endpoint);
    SOAP_ASSERT(speed_Pan <= 100 && speed_Pan >= -100);
    SOAP_ASSERT(speed_Tilt <= 100 && speed_Tilt >= -100);
    SOAP_ASSERT(speed_Zoom <= 100 && speed_Zoom >= -100);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    memset(&tptz__ContinuousMove, 0x00, sizeof(tptz__ContinuousMove));
    memset(&tptz__ContinuousMoveresponse, 0x00, sizeof(tptz__ContinuousMoveresponse));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

	struct tt__PTZSpeed* velocity = soap_new_tt__PTZSpeed(soap, -1);
    SOAP_ASSERT(NULL != velocity);
	tptz__ContinuousMove.Velocity = velocity;

	struct tt__Vector2D* panTilt = soap_new_tt__Vector2D(soap, -1);
    SOAP_ASSERT(NULL != panTilt);
	tptz__ContinuousMove.Velocity->PanTilt = panTilt;

	struct tt__Vector1D* zoom = soap_new_tt__Vector1D(soap, -1);
    SOAP_ASSERT(NULL != zoom);
	tptz__ContinuousMove.Velocity->Zoom = zoom;

	tptz__ContinuousMove.ProfileToken = profile;
	tptz__ContinuousMove.Velocity->Zoom->x = (float)speed_Zoom / 100;
	tptz__ContinuousMove.Velocity->PanTilt->x = (float)speed_Pan / 100;
	tptz__ContinuousMove.Velocity->PanTilt->y = (float)speed_Tilt / 100;
	tptz__ContinuousMove.Velocity->PanTilt->space = "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace";

	result = soap_call___tptz__ContinuousMove(soap, endpoint, NULL, &tptz__ContinuousMove, &tptz__ContinuousMoveresponse);
	SOAP_CHECK_ERROR(result, soap, "soap_call___tptz__ContinuousMove");

EXIT:
    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}

int ONVIF_PTZ_AbsoluteMove(char *profile, char *endpoint, int pos_Pan, int pos_Tilt, int pos_Zoom)
{
	struct _tptz__AbsoluteMove			tptz__AbsoluteMove;
	struct _tptz__AbsoluteMoveResponse  tptz__AbsoluteMoveResponse;
    struct soap *soap = NULL;
	int result = 0;

    SOAP_ASSERT(NULL != profile);
    SOAP_ASSERT(NULL != endpoint);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

    memset(&tptz__AbsoluteMove, 0x00, sizeof(tptz__AbsoluteMove));
    memset(&tptz__AbsoluteMoveResponse, 0x00, sizeof(tptz__AbsoluteMoveResponse));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

	struct tt__PTZVector* vector = soap_new_tt__PTZVector(soap, -1);
    SOAP_ASSERT(NULL != vector);
	tptz__AbsoluteMove.Position = vector;

	struct tt__Vector2D* panTilt = soap_new_tt__Vector2D(soap, -1);
    SOAP_ASSERT(NULL != panTilt);
	tptz__AbsoluteMove.Position->PanTilt = panTilt;

	struct tt__Vector1D* zoom = soap_new_tt__Vector1D(soap, -1);
    SOAP_ASSERT(NULL != zoom);
	tptz__AbsoluteMove.Position->Zoom = zoom;

	tptz__AbsoluteMove.ProfileToken = profile;
	tptz__AbsoluteMove.Position->Zoom->x = (float)pos_Zoom   / 100;
	tptz__AbsoluteMove.Position->PanTilt->x = (float)pos_Pan / 100;
	tptz__AbsoluteMove.Position->PanTilt->y = (float)pos_Tilt / 100;

	printf("p:%f  t: %f z: %f\n", tptz__AbsoluteMove.Position->PanTilt->x, tptz__AbsoluteMove.Position->PanTilt->y , tptz__AbsoluteMove.Position->Zoom->x);

	struct tt__PTZSpeed* s_velocity = soap_new_tt__PTZSpeed(soap, -1);
    SOAP_ASSERT(NULL != s_velocity);
	tptz__AbsoluteMove.Speed = s_velocity;

	struct tt__Vector2D* s_panTilt = soap_new_tt__Vector2D(soap, -1);
    SOAP_ASSERT(NULL != s_panTilt);
	tptz__AbsoluteMove.Speed->PanTilt = s_panTilt;

	struct tt__Vector1D* s_zoom = soap_new_tt__Vector1D(soap, -1);
    SOAP_ASSERT(NULL != s_zoom);
	tptz__AbsoluteMove.Speed->Zoom = s_zoom;

	tptz__AbsoluteMove.ProfileToken = profile;
	tptz__AbsoluteMove.Speed->Zoom->x = (float)(100 / 100);
	tptz__AbsoluteMove.Speed->PanTilt->x = (float)1;
	tptz__AbsoluteMove.Speed->PanTilt->y = (float)1;

	printf("p:%f  t: %f z: %f\n",  tptz__AbsoluteMove.Speed->PanTilt->x,  tptz__AbsoluteMove.Speed->PanTilt->y, tptz__AbsoluteMove.Speed->Zoom->x);
	result = soap_call___tptz__AbsoluteMove(soap, endpoint, NULL, &tptz__AbsoluteMove, &tptz__AbsoluteMoveResponse);
	SOAP_CHECK_ERROR(result, soap, "soap_call___tptz__AbsoluteMove");

EXIT:
    if (NULL != soap) {
        ONVIF_soap_delete(soap);
    }

    return result;
}

/************************************************************************
**������ONVIF_PTZ_Stop
**���ܣ�ʹ�豸ֹͣ�ƶ�
**������
        [in] profile    - MediaProfile������ 
        [in] endpoint   - �豸�����ַ
**���أ�
        0�����ɹ�����0����ʧ��
**��ע��
		��
************************************************************************/
int ONVIF_PTZ_Stop(char *profile, char *endpoint)
{
	struct _tptz__Stop 							stop;
	struct _tptz__StopResponse 					stopresponse;
    struct soap *soap = NULL;
	int result = 0;

    SOAP_ASSERT(NULL != profile);
    SOAP_ASSERT(NULL != endpoint);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

	memset(&stop, 0, sizeof (stop));
	memset(&stopresponse, 0, sizeof (stopresponse));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

	stop.ProfileToken = profile;
	enum xsd__boolean *PT_boolean = soap_new_xsd__boolean(soap, -1);
	enum xsd__boolean *Z_boolean = soap_new_xsd__boolean(soap, -1);
	*PT_boolean = xsd__boolean__true_;
	*Z_boolean = xsd__boolean__true_;
	stop.PanTilt = PT_boolean;
	stop.Zoom = Z_boolean;

	result = soap_call___tptz__Stop(soap, endpoint, NULL, &stop, &stopresponse);
	SOAP_CHECK_ERROR(result, soap, "soap_call__tptz__Stop");

EXIT:
	if (NULL != soap) {
		ONVIF_soap_delete(soap);
	}

	return result;
}

/************************************************************************
**������ONVIF_PTZ_GotoHomePosition
**���ܣ�ʹ�豸��PTZֵ���ص��趨�ĳ�ʼ״̬
**������
        [in] profile    - MediaProfile������ 
        [in] endpoint   - �豸�����ַ
        [in] speed_Pan  - ˮƽ�ƶ��ٶ�, ȡֵ��Χ(-100, +100)
        [in] speed_Tilt - ��б���ƶ��ٶ�, ȡֵ��Χ(-100, +100)
        [in] speed_Zoom - ������ƶ��ٶ�, ȡֵ��Χ(-100, +100) 
**���أ�
        0�����ɹ�����0����ʧ��
**��ע��
		��
************************************************************************/
int ONVIF_PTZ_GotoHomePosition(char *profile, char *endpoint, int speed_Pan, int speed_Tilt, int speed_Zoom)
{
    struct soap *soap = NULL;
	int result = 0;
	struct _tptz__GotoHomePosition			tptz__GotoHomePosition; 
	struct _tptz__GotoHomePositionResponse  tptz__GotoHomePositionResponse;

    SOAP_ASSERT(NULL != profile);
    SOAP_ASSERT(NULL != endpoint);
    SOAP_ASSERT(speed_Pan <= 100 && speed_Pan >= -100);
    SOAP_ASSERT(speed_Tilt <= 100 && speed_Tilt >= -100);
    SOAP_ASSERT(speed_Zoom <= 100 && speed_Zoom >= -100);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));
	
	memset(&tptz__GotoHomePosition, 0, sizeof (tptz__GotoHomePosition));
	memset(&tptz__GotoHomePositionResponse, 0, sizeof (tptz__GotoHomePositionResponse));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

	struct tt__PTZSpeed* velocity = soap_new_tt__PTZSpeed(soap, -1);
    SOAP_ASSERT(NULL != velocity);
	tptz__GotoHomePosition.Speed = velocity;

	struct tt__Vector2D* panTilt = soap_new_tt__Vector2D(soap, -1);
    SOAP_ASSERT(NULL != panTilt);
	tptz__GotoHomePosition.Speed->PanTilt = panTilt;

	struct tt__Vector1D* zoom = soap_new_tt__Vector1D(soap, -1);
    SOAP_ASSERT(NULL != zoom);
	tptz__GotoHomePosition.Speed->Zoom = zoom;

	tptz__GotoHomePosition.ProfileToken = profile;
	tptz__GotoHomePosition.Speed->Zoom->x = (float)speed_Zoom / 100;
	tptz__GotoHomePosition.Speed->PanTilt->x = (float)speed_Pan / 100;
	tptz__GotoHomePosition.Speed->PanTilt->y = (float)speed_Tilt / 100;
	tptz__GotoHomePosition.Speed->PanTilt->space = "http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace";

	result = soap_call___tptz__GotoHomePosition(soap, endpoint, NULL, &tptz__GotoHomePosition, &tptz__GotoHomePositionResponse);
	SOAP_CHECK_ERROR(result, soap, "soap_call___tptz__GotoHomePosition");

EXIT:
	if (NULL != soap) {
		ONVIF_soap_delete(soap);
	}

	return result;
}

/************************************************************************
**������ONVIF_PTZ_SetHomePosition
**���ܣ��趨��ǰPTZ��ֵΪ��ʼ״̬
**������
        [in] profile    - MediaProfile������ 
        [in] endpoint   - �豸�����ַ
**���أ�
        0�����ɹ�����0����ʧ��
**��ע��
		��
************************************************************************/
int ONVIF_PTZ_SetHomePosition(char *profile,char *endpoint)
{
	struct _tptz__SetHomePosition homePosition;
	struct _tptz__SetHomePositionResponse homePositionResponse;
    struct soap *soap = NULL;
	int result = 0;

    SOAP_ASSERT(NULL != profile);
    SOAP_ASSERT(NULL != endpoint);
    SOAP_ASSERT(NULL != (soap = ONVIF_soap_new(SOAP_SOCK_TIMEOUT)));

	memset(&homePosition, 0, sizeof (homePosition));
	memset(&homePositionResponse, 0, sizeof (homePositionResponse));

    ONVIF_SetAuthInfo(soap, USERNAME, PASSWORD);

	homePosition.ProfileToken = profile;
	result = soap_call___tptz__SetHomePosition(soap, endpoint, NULL, &homePosition, &homePositionResponse);
	SOAP_CHECK_ERROR(result, soap, "soap_call___tptz__GotoHomePosition");

EXIT:
	if (NULL != soap) {
		ONVIF_soap_delete(soap);
	}

	return result;
}

/************************************************************************
**������make_uri_withauth
**���ܣ����������֤��Ϣ��URI��ַ
**������
        [in]  src_uri       - δ����֤��Ϣ��URI��ַ
        [in]  username      - �û���
        [in]  password      - ����
        [out] dest_uri      - ���صĴ���֤��Ϣ��URI��ַ
        [in]  size_dest_uri - dest_uri�����С
**���أ�
        0�ɹ�����0ʧ��
**��ע��
    1). ���ӣ�
    ����֤��Ϣ��uri��rtsp://100.100.100.140:554/av0_0
    ����֤��Ϣ��uri��rtsp://username:password@100.100.100.140:554/av0_0
************************************************************************/
int make_uri_withauth(char *src_uri, char *username, char *password, char *dest_uri, unsigned int size_dest_uri)
{
    int result = 0;
    unsigned int needBufSize = 0;

    SOAP_ASSERT(NULL != src_uri);
    SOAP_ASSERT(NULL != username);
    SOAP_ASSERT(NULL != password);
    SOAP_ASSERT(NULL != dest_uri);
    memset(dest_uri, 0x00, size_dest_uri);

    needBufSize = strlen(src_uri) + strlen(username) + strlen(password) + 3;    // ��黺���Ƿ��㹻��������:���͡�@�����ַ���������
    if (size_dest_uri < needBufSize) {
        SOAP_DBGERR("dest uri buf size is not enough.\n");
        result = -1;
        goto EXIT;
    }

    if (0 == strlen(username) && 0 == strlen(password)) {                       // �����µ�uri��ַ
        strcpy(dest_uri, src_uri);
    } else {
        char *p = strstr(src_uri, "//");
        if (NULL == p) {
            SOAP_DBGERR("can't found '//', src uri is: %s.\n", src_uri);
            result = -1;
            goto EXIT;
        }
        p += 2;

        memcpy(dest_uri, src_uri, p - src_uri);
        sprintf(dest_uri + strlen(dest_uri), "%s:%s@", username, password);
        strcat(dest_uri, p);
    }

EXIT:

    return result;
}
