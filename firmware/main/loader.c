/*
 * todo
 * watch driver queue
*/

#include "loader.h"
#include "esp_log.h"
#include "driver.h"
#include "mallocs.h"
#include "ioexp.h"
#include "dacstream.h"

static const char* TAG = "Loader";

EventGroupHandle_t Loader_Status;
StaticEventGroup_t Loader_StatusBuf;
EventGroupHandle_t Loader_BufStatus;
StaticEventGroup_t Loader_BufStatusBuf;
FILE *Loader_File;
FILE *Loader_PcmFile;
VgmInfoStruct_t *Loader_VgmInfo;
uint8_t Loader_VgmDataBlockIndex = 0;
VgmDataBlockStruct_t Loader_VgmDataBlocks[MAX_REALTIME_DATABLOCKS];
bool Loader_RequestedDacStreamFindStart = false;

bool Loader_Setup() {
    ESP_LOGI(TAG, "Setting up");
    
    ESP_LOGI(TAG, "Creating status event group");
    Loader_Status = xEventGroupCreateStatic(&Loader_StatusBuf);
    if (Loader_Status == NULL) {
        ESP_LOGE(TAG, "Failed !!");
        return false;
    }
    xEventGroupSetBits(Loader_Status, LOADER_STOPPED);
    
    ESP_LOGI(TAG, "Creating buffer status event group");
    Loader_BufStatus = xEventGroupCreateStatic(&Loader_BufStatusBuf);
    if (Loader_BufStatus == NULL) {
        ESP_LOGE(TAG, "Failed !!");
        return false;
    }


    return true;
}

uint32_t Loader_PcmPos = 0;
uint32_t Loader_PcmOff = 0;
uint32_t Loader_GetPcmOffset(uint32_t PcmPos) {
    uint32_t consumed = 0;
    for (uint8_t i=0;i<Loader_VgmDataBlockIndex;i++) {
        if (Loader_VgmDataBlocks[i].Type == 0) { //0 = ym2612 pcm
            if (PcmPos >= consumed && PcmPos < consumed + Loader_VgmDataBlocks[i].Size) {
                return Loader_VgmDataBlocks[i].Offset + (PcmPos - consumed);
            }
            consumed += Loader_VgmDataBlocks[i].Size;
        }
    }
    return 0xffffffff;
}

uint32_t Loader_Pending = 0;
uint8_t running = false;
bool Loader_EndReached = false;
uint8_t Loader_PcmBuf[FREAD_LOCAL_BUF];
uint16_t Loader_PcmBufUsed = FREAD_LOCAL_BUF;
void Loader_Main() {
    ESP_LOGI(TAG, "Task start");
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(Loader_Status, LOADER_START_REQUEST | LOADER_RUNNING | LOADER_STOP_REQUEST, false, false, pdMS_TO_TICKS(75));
        if (bits & LOADER_START_REQUEST) {
            ESP_LOGI(TAG, "Loader starting");
            xEventGroupClearBits(Loader_Status, LOADER_STOPPED);
            xEventGroupSetBits(Loader_Status, LOADER_RUNNING);
            xEventGroupClearBits(Loader_Status, LOADER_START_REQUEST);
            running = true;
        } else if (bits & LOADER_STOP_REQUEST) {
            ESP_LOGI(TAG, "Loader stopping");
            running = false;
            xEventGroupClearBits(Loader_Status, LOADER_RUNNING);
            xEventGroupSetBits(Loader_Status, LOADER_STOPPED);
            xEventGroupClearBits(Loader_Status, LOADER_STOP_REQUEST);
        }
        if (running) {
            uint16_t spaces = uxQueueSpacesAvailable(Driver_CommandQueue);
            EventBits_t bbits = xEventGroupGetBits(Loader_BufStatus);
            if (spaces == 0 && !(bbits & LOADER_BUF_FULL)) {
                xEventGroupSetBits(Loader_BufStatus, LOADER_BUF_FULL);
                xEventGroupClearBits(Loader_BufStatus, 0xff ^ LOADER_BUF_FULL);
            } else if (spaces == DRIVER_QUEUE_SIZE) {
                xEventGroupSetBits(Loader_BufStatus, LOADER_BUF_EMPTY);
                xEventGroupClearBits(Loader_BufStatus, 0xff ^ LOADER_BUF_EMPTY);
            } else if (spaces < DRIVER_QUEUE_SIZE/6 || Loader_EndReached) {
                xEventGroupSetBits(Loader_BufStatus, LOADER_BUF_OK);
                xEventGroupClearBits(Loader_BufStatus, 0xff ^ LOADER_BUF_OK);
            } else if (spaces >= DRIVER_QUEUE_SIZE/2) {
                xEventGroupSetBits(Loader_BufStatus, LOADER_BUF_LOW);
                xEventGroupClearBits(Loader_BufStatus, 0xff ^ LOADER_BUF_LOW);
            }
            if (spaces > DRIVER_QUEUE_SIZE/6) {
                //IoExp_WriteLed(0, true);
                while (uxQueueSpacesAvailable(Driver_CommandQueue)) {
                    uint8_t d;
                    fread(&d,1,1,Loader_File);
                    if (Loader_Pending == 0) {
                        if (d == 0xe0) { //pcm seek
                            uint32_t NewPos = 0;
                            fread(&NewPos,4,1,Loader_File);
                            uint32_t NewOff = Loader_GetPcmOffset(NewPos);
                            if (NewOff != (Loader_PcmOff+1)) {
                                ESP_LOGD(TAG, "Pcm seeking to %d", NewOff);
                                fseek(Loader_PcmFile, NewOff, SEEK_SET);
                                Loader_PcmOff = NewOff;
                                Loader_PcmBufUsed = FREAD_LOCAL_BUF;
                            }
                            Loader_PcmPos = NewPos;
                            continue;
                        } else if (d == 0x67) { //datablock
                            if (Loader_VgmDataBlockIndex == MAX_REALTIME_DATABLOCKS) {
                                ESP_LOGE(TAG, "loader datablocks over !!");
                                return;
                            } else {
                                VgmParseDataBlock(Loader_File, &Loader_VgmDataBlocks[Loader_VgmDataBlockIndex++]);
                            }
                            continue;
                        } else if ((d&0xf0) == 0x80) { //pcm and wait
                            if (Loader_PcmOff == 0) { //if this is the first sample being played, need to do an initial seek
                                Loader_PcmOff = Loader_GetPcmOffset(Loader_PcmPos);
                                fseek(Loader_PcmFile, Loader_PcmOff, SEEK_SET);
                                Loader_PcmBufUsed = FREAD_LOCAL_BUF;
                            }
                            if (Loader_PcmBufUsed == FREAD_LOCAL_BUF) {
                                fread(&Loader_PcmBuf[0], 1, FREAD_LOCAL_BUF, Loader_PcmFile); //todo: fix read past eof
                                Loader_PcmBufUsed = 0;
                            }
                            xQueueSendToBack(Driver_PcmQueue, &Loader_PcmBuf[Loader_PcmBufUsed++], 0); //theoretically pcmqueue should never ever be full while there are still spaces in commandqueue
                            Loader_PcmPos++;
                            #ifdef PARANOID_THAT_THERE_MIGHT_BE_VGMS_THAT_PLAY_PCM_ACROSS_BLOCK_BOUNDARIES
                            uint32_t NewOff = Loader_GetPcmOffset(Loader_PcmPos);
                            if (NewOff != (Loader_PcmOff+1)) {
                                ESP_LOGI(TAG, "pcm seeking to %d after sample load", NewOff);
                                fseek(Loader_PcmFile, NewOff, SEEK_SET);
                                Loader_PcmOff = NewOff;
                                Loader_PcmBufUsed = FREAD_LOCAL_BUF;
                            }
                            #else
                            Loader_PcmOff++;
                            #endif
                        } else if (d == 0x66) { //end of music, optionally loop
                            Loader_EndReached = true;
                            fseek(Loader_File, Loader_VgmInfo->LoopOffset, SEEK_SET);
                            continue;
                        } else if (d >= 0x90 && d <= 0x95) { //dacstream command
                            if (!Loader_RequestedDacStreamFindStart) {
                                DacStream_BeginFinding(&Loader_VgmDataBlocks, Loader_VgmDataBlockIndex, ftell(Loader_File)-1);
                                Loader_RequestedDacStreamFindStart = true;
                            }
                            if (VgmCommandIsFixedSize(d)) {
                                Loader_Pending = VgmCommandLength(d) - 1;
                            } else {
                                ESP_LOGE(TAG, "non-fixed size command unimplemented !!");
                            }
                        } else {
                            if (VgmCommandIsFixedSize(d)) {
                                Loader_Pending = VgmCommandLength(d) - 1;
                            } else {
                                ESP_LOGE(TAG, "non-fixed size command unimplemented !!");
                            }
                        }
                    } else {
                        Loader_Pending--;
                    }
                    xQueueSendToBack(Driver_CommandQueue, &d, 0);
                }
                //IoExp_WriteLed(0, false);
            } else {
            }
        }
        vTaskDelay(pdMS_TO_TICKS(75));
    }
}

bool Loader_Start(FILE *File, FILE *PcmFile, VgmInfoStruct_t *info) {
    if (xEventGroupGetBits(Loader_Status) & LOADER_RUNNING) {
        //running, can't start
        return false;
    }

    Loader_File = File;
    Loader_PcmFile = PcmFile;
    Loader_VgmInfo = info;
    Loader_PcmPos = 0;
    Loader_PcmOff = 0;
    Loader_Pending = 0;
    Loader_EndReached = false;
    Loader_RequestedDacStreamFindStart = false;
    Loader_VgmDataBlockIndex = 0;
    
    fseek(Loader_File, Loader_VgmInfo->DataOffset, SEEK_SET);

    xEventGroupSetBits(Loader_Status, LOADER_START_REQUEST);

    return true;
}

bool Loader_Stop() {
    if (xEventGroupGetBits(Loader_Status) & LOADER_STOPPED) {
        ESP_LOGE(TAG, "Bugcheck: Loader_Stop() called but loader is already stopped !!");
        return false;
    }
    xEventGroupSetBits(Loader_Status, LOADER_STOP_REQUEST);
    EventBits_t bits = xEventGroupWaitBits(Loader_Status, LOADER_STOPPED, false, false, pdMS_TO_TICKS(3000));
    if (bits & LOADER_STOPPED) {
        //cleanup stuff
        Loader_VgmDataBlockIndex = 0;
        xEventGroupClearBits(Loader_BufStatus, 0xff);
        xQueueReset(Driver_CommandQueue);
        xQueueReset(Driver_PcmQueue);
        return true;
    } else {
        ESP_LOGE(TAG, "Loader stop request timeout !!");
        return false;
    }
}