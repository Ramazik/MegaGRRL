#include "player.h"
#include "esp_log.h"
#include "unistd.h"
#include "driver.h"
#include "ioexp.h"
#include "loader.h"
#include "dacstream.h"
#include "freertos/event_groups.h"
#include "clk.h"
#include "queue.h"
#include "vgm.h"
#include "gd3.h"
#include "mallocs.h"
#include "ui/nowplaying.h"
#include "rom/miniz.h"
#include "ui/statusbar.h"

static const char* TAG = "Player";

FILE *Player_VgmFile;
FILE *Player_PcmFile;
FILE *Player_DsFindFile;
FILE *Player_DsFillFile;
VgmInfoStruct_t Player_Info;
uint32_t notif = 0;

volatile uint8_t Player_SetLoopCount = 2;
volatile uint8_t Player_LoopCount = 2;
volatile RepeatMode_t Player_RepeatMode = REPEAT_ALL;

EventGroupHandle_t Player_Status;
StaticEventGroup_t Player_StatusBuf;

char Player_Gd3_Title[PLAYER_GD3_FIELD_SIZES+1];
char Player_Gd3_Game[PLAYER_GD3_FIELD_SIZES+1];
char Player_Gd3_Author[PLAYER_GD3_FIELD_SIZES+1];

bool stopped = true;

bool Player_NextTrk(bool UserSpecified) { //returns true if there is now a track playing
    Player_StopTrack();
    if (!UserSpecified && Player_RepeatMode == REPEAT_ONE) {
        //nothing to do - just start the same track again
    } else {
        bool end = (QueuePosition == QueueLength-1);
        if (end) {
            if (Player_RepeatMode == REPEAT_NONE) {
                return false; //nothing more to play
            } else if (Player_RepeatMode == REPEAT_ALL || Player_RepeatMode == REPEAT_ONE) {
                QueuePosition = 0;
                QueueSetupEntry(false);
            }
        } else { //not the end of the queue, just load the next track
            QueueNext();
            QueueSetupEntry(false);
        }
    }
    Player_StartTrack(&QueuePlayingFilename[0]);
    return true;
}

bool Player_PrevTrk(bool UserSpecified) { //returns true if there is now a track playing
    Player_StopTrack();
    if (!UserSpecified && Player_RepeatMode == REPEAT_ONE) {
        //nothing to do - just start the same track again
    } else {
        bool end = (QueuePosition == 0);
        if (end) {
            if (Player_RepeatMode == REPEAT_NONE) {
                //nothing to do - just start the same track again
            } else if (Player_RepeatMode == REPEAT_ALL || Player_RepeatMode == REPEAT_ONE) {
                QueuePosition = QueueLength-1;
                QueueSetupEntry(false);
            }
        } else { //not the end of the queue, just load the prev track
            QueuePrev();
            QueueSetupEntry(false);
        }
    }
    Player_StartTrack(&QueuePlayingFilename[0]);
    return true;
}

void Player_Main() {
    ESP_LOGI(TAG, "Task start");

    while (1) {
        if (xTaskNotifyWait(0,0xffffffff, &notif, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (notif == PLAYER_NOTIFY_START_RUNNING) {
                ESP_LOGI(TAG, "control: start requested");
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_RAN_OUT);
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_PAUSED);
                if ((xEventGroupGetBits(Player_Status) & PLAYER_STATUS_RUNNING) == 0) {
                    xEventGroupSetBits(Player_Status, PLAYER_STATUS_RUNNING); //do this now, Player_StartTrack could take longer than the timeout of the task waiting for this event.
                    QueueSetupEntry(false);
                    Player_StartTrack(&QueuePlayingFilename[0]);
                } else {
                    //already running. yikes!
                }
            } else if (notif == PLAYER_NOTIFY_STOP_RUNNING) {
                ESP_LOGI(TAG, "control: stop requested");
                if (xEventGroupGetBits(Player_Status) & PLAYER_STATUS_RUNNING) {
                    ESP_LOGI(TAG, "player running, stopping track");
                    Player_StopTrack();
                }
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_RUNNING);
                xEventGroupSetBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_RAN_OUT);
            } else if (notif == PLAYER_NOTIFY_NEXT) {
                ESP_LOGI(TAG, "control: next requested");
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_PAUSED);
                if (Player_NextTrk(true)) {
                    ESP_LOGI(TAG, "next track proceeding");
                    xEventGroupSetBits(Player_Status, PLAYER_STATUS_RUNNING);
                    xEventGroupClearBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                } else {
                    ESP_LOGI(TAG, "next track failed");
                    xEventGroupClearBits(Player_Status, PLAYER_STATUS_RUNNING);
                    xEventGroupSetBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                    xEventGroupSetBits(Player_Status, PLAYER_STATUS_RAN_OUT);
                }
            } else if (notif == PLAYER_NOTIFY_PREV) {
                ESP_LOGI(TAG, "control: prev requested");
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_PAUSED);
                if (Driver_Sample < 3*44100) { //actually change track
                    ESP_LOGI(TAG, "within 3 second window");
                    if (Player_PrevTrk(true)) {
                        ESP_LOGI(TAG, "prev track proceeding");
                        xEventGroupSetBits(Player_Status, PLAYER_STATUS_RUNNING);
                        xEventGroupClearBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                    } else {
                        ESP_LOGI(TAG, "prev track failed");
                        xEventGroupClearBits(Player_Status, PLAYER_STATUS_RUNNING);
                        xEventGroupSetBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                    }
                } else { //just restart
                    ESP_LOGI(TAG, "outside 3 second window, just restarting track");
                    Player_StopTrack();
                    Player_StartTrack(&QueuePlayingFilename[0]);
                    xEventGroupSetBits(Player_Status, PLAYER_STATUS_RUNNING);
                    xEventGroupClearBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                }
            } else if (notif == PLAYER_NOTIFY_PAUSE) {
                ESP_LOGI(TAG, "control: pause requested");
                if (xEventGroupGetBits(Driver_CommandEvents) & DRIVER_EVENT_RUNNING) {
                    ESP_LOGI(TAG, "Request driver stop...");
                    xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_STOP_REQUEST);
                    /*ESP_LOGI(TAG, "Wait for driver to stop...");
                    if (xEventGroupWaitBits(Driver_CommandEvents, DRIVER_EVENT_RUNNING, false, false, pdMS_TO_TICKS(3000)) & DRIVER_EVENT_RUNNING) {
                        ESP_LOGE(TAG, "Driver stop timeout !!");
                        return false;
                    }*/
                }
                xEventGroupSetBits(Player_Status, PLAYER_STATUS_PAUSED);
            } else if (notif == PLAYER_NOTIFY_PLAY) {
                ESP_LOGI(TAG, "control: play requested");
                if ((xEventGroupGetBits(Driver_CommandEvents) & DRIVER_EVENT_RUNNING) == 0) {
                    ESP_LOGI(TAG, "Request driver resume...");
                    xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_RESUME_REQUEST);
                    ESP_LOGI(TAG, "Wait for driver to resume...");
                    if ((xEventGroupWaitBits(Driver_CommandEvents, DRIVER_EVENT_RUNNING, false, false, pdMS_TO_TICKS(3000)) & DRIVER_EVENT_RUNNING) == 0) {
                        ESP_LOGE(TAG, "Driver resume timeout !!");
                        return false;
                    }
                }
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_PAUSED);
            }
        } else { //no incoming notification

        }
        if ((xEventGroupGetBits(Player_Status) & PLAYER_STATUS_RUNNING) && (xEventGroupGetBits(Driver_CommandEvents) & DRIVER_EVENT_FINISHED)) { //still running, but driver reached end
            ESP_LOGI(TAG, "Driver finished, starting next track");
            xEventGroupClearBits(Player_Status, PLAYER_STATUS_PAUSED);
            if (Player_NextTrk(false)) {
                ESP_LOGI(TAG, "next track proceeding");
                //nothing to do, i don't think...
            } else {
                ESP_LOGI(TAG, "next track failed");
                xEventGroupClearBits(Player_Status, PLAYER_STATUS_RUNNING);
                xEventGroupSetBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
                xEventGroupSetBits(Player_Status, PLAYER_STATUS_RAN_OUT);
            }
        }
    }
}

bool Player_Setup() {
    ESP_LOGI(TAG, "Setting up");

    Player_Gd3_Title[0] = 0;
    Player_Gd3_Game[0] = 0;
    Player_Gd3_Author[0] = 0;

    ESP_LOGI(TAG, "Creating status event group");
    Player_Status = xEventGroupCreateStatic(&Player_StatusBuf);
    if (Player_Status == NULL) {
        ESP_LOGE(TAG, "Failed !!");
        return false;
    }
    xEventGroupSetBits(Player_Status, PLAYER_STATUS_NOT_RUNNING);
    return true;
}

static tinfl_decompressor decomp;
void Player_Unvgz(char *FilePath, bool ReplaceOriginalFile) {
    FILE *reader;
    FILE *writer;

    if (ReplaceOriginalFile) {
        char vgmfn[513];
        strcpy(vgmfn, FilePath);
        vgmfn[strlen(vgmfn)-1] = 'm';
        
        ESP_LOGW(TAG, "Unvgz: Decompressing %s to %s", FilePath, vgmfn);

        //copy vgz to temp
        uint32_t copysize;
        reader = fopen(FilePath, "r");
        writer = fopen("/sd/.mega/unvgz.tmp", "w");
        do {
            copysize = fread(Driver_PcmBuf, 1, DACSTREAM_BUF_SIZE*DACSTREAM_PRE_COUNT, reader);
            fwrite(Driver_PcmBuf, 1, copysize, writer);
        } while (copysize == DACSTREAM_BUF_SIZE*DACSTREAM_PRE_COUNT);
        fclose(reader);
        fclose(writer);

        //rename existing vgz to the new vgm
        rename(FilePath, vgmfn);

        //open new files
        reader = fopen("/sd/.mega/unvgz.tmp", "r");
        writer = fopen(vgmfn, "r+");
    } else {
        ESP_LOGW(TAG, "Unvgz: Decompressing %s to temp file", FilePath);
        reader = fopen(FilePath, "r");
        writer = fopen("/sd/.mega/unvgz.tmp", "w");
    }
    fseek(writer, 0, SEEK_SET);

    //get compressed size
    fseek(reader, 0, SEEK_END);
    size_t in_remaining = ftell(reader) - 18; //18 = gzip header size. TODO check gzip header for sanity
    fseek(reader, 10, SEEK_SET);

    tinfl_init(&decomp);
    const void *next_in = Driver_CommandQueueBuf;
    void *next_out = Driver_PcmBuf;
    size_t avail_in = 0;
    size_t avail_out = 65536; // >= LZ dict size*2 && <= DACSTREAM_BUF_SIZE*DACSTREAM_PRE_COUNT}
    size_t total_in = 0;
    size_t total_out = 0;
    size_t in_bytes, out_bytes;
    tinfl_status status;
    for (;;) {
        if (!avail_in) {
            size_t rd = (in_remaining<16384)?in_remaining:16384; //power of 2 <= DRIVER_QUEUE_SIZE
            if (fread(Driver_CommandQueueBuf, 1, rd, reader) != rd) {
                ESP_LOGE(TAG, "read fail");
            }
            ESP_LOGD(TAG, "read chunk %d", rd);
            next_in = Driver_CommandQueueBuf;
            avail_in = rd;
            in_remaining -= rd;
        }
        in_bytes = avail_in;
        out_bytes = avail_out;
        ESP_LOGD(TAG, "inb %d outb %d", in_bytes, out_bytes);
        status = tinfl_decompress(&decomp, (const mz_uint8 *)next_in, &in_bytes, Driver_PcmBuf, (mz_uint8 *)next_out, &out_bytes, (in_remaining?TINFL_FLAG_HAS_MORE_INPUT:0)/*|TINFL_FLAG_PARSE_ZLIB_HEADER*/);

        avail_in -= in_bytes;
        next_in = (const mz_uint8 *)next_in + in_bytes;
        total_in += in_bytes;
        
        avail_out -= out_bytes;
        next_out = (mz_uint8 *)next_out + out_bytes;
        total_out += out_bytes;

        if ((status <= TINFL_STATUS_DONE) || (!avail_out)) {
            size_t wr = 65536 - avail_out;
            fwrite(Driver_PcmBuf, 1, wr, writer);
            ESP_LOGD(TAG, "wrote chunk %d", wr);
            next_out = Driver_PcmBuf;
            avail_out = 65536;
        }

        if (status <= TINFL_STATUS_DONE) {
            if (status == TINFL_STATUS_DONE) {
                ESP_LOGI(TAG, "decomp ok !!");
                break;
            } else {
                ESP_LOGE(TAG, "decomp fail %d", status);
            }
        }
    }
    fclose(reader);
    fclose(writer);
}

bool Player_StartTrack(char *FilePath) {
    ESP_LOGI(TAG, "Checking file type of %s", FilePath);
    FILE *test = fopen(FilePath, "r");
    if (!test) {
        if (*(FilePath+(strlen(FilePath)-1)) == 'z' || *(FilePath+(strlen(FilePath)-1)) == 'Z') {
            ESP_LOGW(TAG, "vgz doesn't exist, let's try vgm");
            *(FilePath+(strlen(FilePath)-1)) -= 0x0d;
            test = fopen(FilePath, "r");
            if (!test) {
                ESP_LOGE(TAG, "vgm doesn't exist either");
                return false;
            }
        } else {
            ESP_LOGE(TAG, "file doesn't exist");
            return false;
        }
    }
    uint16_t magic = 0;
    fseek(test, 0, SEEK_SET);
    fread(&magic, 2, 1, test);
    fclose(test);
    if (magic == 0x8b1f) {
        ESP_LOGI(TAG, "Compressed");
        Ui_StatusBar_SetExtract(true);
        Player_Unvgz(FilePath, false);
        strcpy(FilePath, "/sd/.mega/unvgz.tmp");
        Ui_StatusBar_SetExtract(false);
    } else if (magic == 0x6756) {
        ESP_LOGI(TAG, "Uncompressed");
    } else {
        ESP_LOGI(TAG, "Unknown");
        return false;
    }
    ESP_LOGI(TAG, "parsing header");
    Player_VgmFile = fopen(FilePath, "r");
    Player_PcmFile = fopen(FilePath, "r");
    Player_DsFindFile = fopen(FilePath, "r");
    Player_DsFillFile = fopen(FilePath, "r");
    Driver_Opna_PcmUploadFile = fopen(FilePath, "r");
    fseek(Player_VgmFile, 0, SEEK_SET);
    fseek(Player_DsFindFile, 0, SEEK_SET);
    VgmParseHeader(Player_VgmFile, &Player_Info);

    Gd3Descriptor_t desc;
    Gd3ParseDescriptor(Player_VgmFile, &Player_Info, &desc);
    if (desc.parsed) {
        Gd3GetStringChars(Player_VgmFile, &desc, GD3STRING_TRACK_EN, &Player_Gd3_Title[0], PLAYER_GD3_FIELD_SIZES);
        Gd3GetStringChars(Player_VgmFile, &desc, GD3STRING_GAME_EN, &Player_Gd3_Game[0], PLAYER_GD3_FIELD_SIZES);
        Gd3GetStringChars(Player_VgmFile, &desc, GD3STRING_AUTHOR_EN, &Player_Gd3_Author[0], PLAYER_GD3_FIELD_SIZES);
    } else {
        Player_Gd3_Title[0] = 0;
        Player_Gd3_Game[0] = 0;
        Player_Gd3_Author[0] = 0;
    }

    Ui_NowPlaying_DataAvail = true;
    Ui_NowPlaying_NewTrack = true;

    /* todo here:
    * set driver clock rate for wait scaling
    */

    ESP_LOGI(TAG, "vgm rate: %d", Player_Info.Rate);

    //todo: improve this, check that the vgm is for the chips we have
    if (Driver_DetectedMod == MEGAMOD_NONE) {
        ESP_LOGI(TAG, "MegaMod: none");
        uint32_t PsgClock = 0;
        uint32_t FmClock = 0;
        fseek(Player_VgmFile, 0x0c, SEEK_SET);
        fread(&PsgClock, 4, 1, Player_VgmFile);
        fseek(Player_VgmFile, 0x2c, SEEK_SET);
        fread(&FmClock, 4, 1, Player_VgmFile);
        ESP_LOGI(TAG, "Clocks from vgm: psg %d, fm %d", PsgClock, FmClock);

        if (PsgClock == 0) PsgClock = 3579545;
        else if (PsgClock < 3000000) PsgClock = 3000000;
        else if (PsgClock > 4100000) PsgClock = 4100000;
        if (FmClock == 0) FmClock = 7670453;
        else if (FmClock < 7000000) FmClock = 7000000;
        else if (FmClock > 8300000) FmClock = 8300000;
        ESP_LOGI(TAG, "Clocks clamped: psg %d, fm %d", PsgClock, FmClock);
        Clk_Set(CLK_FM, FmClock);
        Clk_Set(CLK_PSG, PsgClock);
    } else if (Driver_DetectedMod == MEGAMOD_OPLLPSG) {
        ESP_LOGI(TAG, "MegaMod: OPLL+PSG");
        Clk_Set(CLK_PSG, 0);
        uint32_t opll = 0;
        uint32_t psg = 0;
        fseek(Player_VgmFile, 0x0c, SEEK_SET);
        fread(&psg,4,1,Player_VgmFile);
        fread(&opll,4,1,Player_VgmFile);
        if ((psg & 0x80000000) || (opll & 0x80000000)) {
            ESP_LOGW(TAG, "Only one of each chip supported !!");
        } else if (psg && opll && (psg != opll)) {
            ESP_LOGW(TAG, "Different clocks not supported !!");
        }
        ESP_LOGI(TAG, "Clock from vgm: PSG: %d, OPLL: %d", psg, opll);
        if (psg == 0) psg = opll;
        if (psg < 3000000) psg = 3000000;
        else if (psg > 4100000) psg = 4100000;
        ESP_LOGI(TAG, "Clock clamped: %d", psg);
        Clk_Set(CLK_FM, psg);
    } else if (Driver_DetectedMod == MEGAMOD_OPNA) {
        ESP_LOGI(TAG, "MegaMod: OPNA");
        Clk_Set(CLK_PSG, 0);
        uint32_t opna = 0;
        uint32_t opn = 0;
        fseek(Player_VgmFile, 0x44, SEEK_SET);
        fread(&opn,4,1,Player_VgmFile);
        fread(&opna,4,1,Player_VgmFile);
        if (opna & 0x80000000) {
            ESP_LOGW(TAG, "Only one opna supported !!");
        }
        if (opna) {
            ESP_LOGI(TAG, "Clock from vgm: %d", opna);
            if (opna < 6000000) opna = 6000000;
            else if (opna > 10000000) opna = 10000000;
            ESP_LOGI(TAG, "Clock clamped: %d", opna);
            Clk_Set(CLK_FM, opna);
        } else if (opn) {
            Clk_Set(CLK_FM, opn<<1);
        }
    } else if (Driver_DetectedMod == MEGAMOD_OPL3) {
        uint32_t opl = 0;
        uint32_t opl2 = 0;
        uint32_t opl3 = 0;
        fseek(Player_VgmFile, 0x50, SEEK_SET);
        fread(&opl2,4,1,Player_VgmFile);
        fread(&opl,4,1,Player_VgmFile);
        fseek(Player_VgmFile, 0x5c, SEEK_SET);
        fread(&opl3,4,1,Player_VgmFile);
        if ((opl & 0x80000000) || (opl2 & 0x80000000) || (opl3 & 0x80000000)) {
            ESP_LOGW(TAG, "Only one of each chip supported FOR NOW !!");
        }
        //todo make this shit work like the above with clamping etc
        if (opl3) {
            ESP_LOGI(TAG, "set %d", opl3);
            Clk_Set(CLK_FM, opl3);
        } else if (opl2) {
            opl2 *= 4;
            ESP_LOGI(TAG, "set %d", opl2);
            Clk_Set(CLK_FM, opl2);
        } else if (opl) {
            opl *= 4;
            ESP_LOGI(TAG, "set %d", opl);
            Clk_Set(CLK_FM, opl);
        }
    }

    ESP_LOGI(TAG, "Signalling driver reset");
    xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_RESET_REQUEST);

    ESP_LOGI(TAG, "Starting dacstreams");
    bool ret;
    ret = DacStream_Start(Player_DsFindFile, Player_DsFillFile, &Player_Info);
    if (!ret) {
        ESP_LOGE(TAG, "Dacstreams failed to start !!");
        return false;
    }

    ESP_LOGI(TAG, "Starting loader");
    ret = Loader_Start(Player_VgmFile, Player_PcmFile, &Player_Info);
    if (!ret) {
        ESP_LOGE(TAG, "Loader failed to start !!");
        return false;
    }

    ESP_LOGI(TAG, "Wait for loader to start...");
    EventBits_t bits;
    bits = xEventGroupWaitBits(Loader_Status, LOADER_RUNNING, false, false, pdMS_TO_TICKS(3000));
    if ((bits & LOADER_RUNNING) == 0) {
        ESP_LOGE(TAG, "Loader start timeout !!");
        return false;
    }

    ESP_LOGI(TAG, "Wait for loader buffer OK...");
    bits = xEventGroupWaitBits(Loader_BufStatus, LOADER_BUF_OK | LOADER_BUF_FULL, false, false, pdMS_TO_TICKS(10000));
    if ((bits & (LOADER_BUF_OK | LOADER_BUF_FULL)) == 0) {
        ESP_LOGE(TAG, "Loader buffer timeout !!");
        return false;
    }

    ESP_LOGI(TAG, "Wait for dacstream fill task...");
    bits = xEventGroupWaitBits(DacStream_FillStatus, DACSTREAM_RUNNING, false, false, pdMS_TO_TICKS(3000));
    if ((bits & DACSTREAM_RUNNING) == 0) {
        ESP_LOGE(TAG, "Dacstream fill task start timeout !!");
        return false;
    }

    ESP_LOGI(TAG, "Wait for driver to reset...");
    bits = xEventGroupWaitBits(Driver_CommandEvents, DRIVER_EVENT_RESET_ACK, false, false, pdMS_TO_TICKS(3000));
    if ((bits & DRIVER_EVENT_RESET_ACK) == 0) {
        ESP_LOGE(TAG, "Driver reset ack timeout !!");
        return false;
    }
    xEventGroupClearBits(Driver_CommandEvents, DRIVER_EVENT_RESET_ACK);

    ESP_LOGI(TAG, "Request driver start...");
    xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_START_REQUEST);

    ESP_LOGI(TAG, "Wait for driver to start...");
    bits = xEventGroupWaitBits(Driver_CommandEvents, DRIVER_EVENT_RUNNING, false, false, pdMS_TO_TICKS(3000));
    if ((bits & DRIVER_EVENT_RUNNING) == 0) {
        ESP_LOGE(TAG, "Driver start timeout !!");
        return false;
    }

    ESP_LOGI(TAG, "Driver started !!");

    return true;
}

bool Player_StopTrack() {
    Ui_NowPlaying_DataAvail = false;

    ESP_LOGI(TAG, "Requesting driver stop...");
    xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_STOP_REQUEST);

    ESP_LOGI(TAG, "Waiting for driver to stop...");
    while (xEventGroupGetBits(Driver_CommandEvents) & DRIVER_EVENT_RUNNING) vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Signalling driver reset");
    xEventGroupSetBits(Driver_CommandEvents, DRIVER_EVENT_RESET_REQUEST);

    ESP_LOGI(TAG, "Requesting loader stop...");
    bool ret = Loader_Stop();
    if (!ret) {
        ESP_LOGE(TAG, "Loader stop timeout !!");
        fclose(Player_VgmFile);
        fclose(Player_PcmFile);
        fclose(Player_DsFindFile);
        fclose(Player_DsFillFile);
        fclose(Driver_Opna_PcmUploadFile);
        return false;
    }

    ESP_LOGI(TAG, "Requesting dacstream stop...");
    ret = DacStream_Stop();
    if (!ret) {
        ESP_LOGE(TAG, "Dacstream stop timeout !!");
        fclose(Player_VgmFile);
        fclose(Player_PcmFile);
        fclose(Player_DsFindFile);
        fclose(Player_DsFillFile);
        fclose(Driver_Opna_PcmUploadFile);
        return false;
    }

    ESP_LOGI(TAG, "Wait for driver to reset...");
    EventBits_t bits = xEventGroupWaitBits(Driver_CommandEvents, DRIVER_EVENT_RESET_ACK, false, false, pdMS_TO_TICKS(3000));
    if ((bits & DRIVER_EVENT_RESET_ACK) == 0) {
        ESP_LOGE(TAG, "Driver reset ack timeout !!");
        return false;
    }
    xEventGroupClearBits(Driver_CommandEvents, DRIVER_EVENT_RESET_ACK);

    fclose(Player_VgmFile);
    fclose(Player_PcmFile);
    fclose(Player_DsFindFile);
    fclose(Player_DsFillFile);
    fclose(Driver_Opna_PcmUploadFile);

    return true;
}