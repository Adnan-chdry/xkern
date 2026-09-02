#ifndef HDA_H
#define HDA_H

#include "types.h"

#define HDA_PCI_CLASS    0x04
#define HDA_PCI_SUBCLASS 0x03
#define HDA_PCI_PROGIF   0x00

#define HDA_CORB_SIZE      256
#define HDA_RIRB_SIZE      256
#define HDA_MAX_CODECS    15
#define HDA_MAX_WIDGETS   64

#define HDA_GCAP       0x00
#define HDA_VMIN       0x02
#define HDA_VMAJ       0x03
#define HDA_OUTPAY     0x04
#define HDA_INPAY      0x06
#define HDA_GCTL       0x08
#define HDA_WAKEEN     0x0C
#define HDA_STATESTS   0x0E
#define HDA_GSTS       0x10
#define HDA_CORBBASE   0x40
#define HDA_RIRBBASE   0x60
#define HDA_DPLBASE    0x70
#define HDA_DPUBASE    0x74

#define HDA_GCTL_CRST    (1 << 0)
#define HDA_GCTL_FCNTRL  (1 << 1)
#define HDA_GCTL_UNSOL   (1 << 8)

#define HDA_CORBCTL      0x0C
#define HDA_CORBSTS      0x0D
#define HDA_CORBRP       0x0A
#define HDA_CORBWP       0x08
#define HDA_CORBSIZE     0x0E

#define HDA_RIRBCTL      0x0C
#define HDA_RIRBSTS      0x0D
#define HDA_RIRBWP       0x08
#define HDA_RIRBSIZE     0x0E

#define HDA_CORBCTL_RUN  (1 << 0)
#define HDA_RIRBCTL_RUN  (1 << 0)
#define HDA_RIRBCTL_OIC  (1 << 2)

#define HDA_VERB_GET_PARAMETER        0xF00
#define HDA_VERB_GET_SUBSYSTEM_ID     0xF20
#define HDA_VERB_GET_CONVERTER_FORMAT 0xA00
#define HDA_VERB_SET_CONVERTER_FORMAT 0x200
#define HDA_VERB_GET_POWER_STATE      0xF05
#define HDA_VERB_SET_POWER_STATE      0x705
#define HDA_VERB_GET_PIN_WIDGET       0xF0C
#define HDA_VERB_SET_PIN_WIDGET       0x70C
#define HDA_VERB_GET_CONNECTION_LIST  0xF02
#define HDA_VERB_GET_AMP_GAIN_MUTE    0xB00

#define HDA_PARAM_VENDOR_ID         0x00
#define HDA_PARAM_REVISION          0x02
#define HDA_PARAM_SUBORDINATE_COUNT 0x04
#define HDA_PARAM_FUNCTION_GROUP    0x05
#define HDA_PARAM_AUDIO_WIDGET      0x09
#define HDA_PARAM_PIN_CAP           0x0C
#define HDA_PARAM_AMP_IN_CAP        0x0D
#define HDA_PARAM_AMP_OUT_CAP       0x12
#define HDA_PARAM_CONNECTION_LIST   0x0E
#define HDA_PARAM_SUPPORTED_PCM     0x0A
#define HDA_PARAM_SUPPORTED_STREAM  0x0B
#define HDA_PARAM_CONFIG_DEFAULT    0x1F

#define HDA_WIDGET_AUDIO_OUTPUT   0x00
#define HDA_WIDGET_AUDIO_INPUT    0x01
#define HDA_WIDGET_AUDIO_MIXER    0x02
#define HDA_WIDGET_AUDIO_SELECTOR 0x03
#define HDA_WIDGET_PIN_COMPLEX    0x04
#define HDA_WIDGET_POWER_WIDGET   0x05
#define HDA_WIDGET_VOLUME_KNOB    0x06
#define HDA_WIDGET_BEEP_GENERATOR 0x07
#define HDA_WIDGET_OTHER          0x0F

struct hda_codec {
    u8   addr;
    u16  vendor_id;
    u16  device_id;
    u32  revision;
    u8   major;
    u8   minor;
    u8   start_node;
    u8   end_node;
    u32  function_group;
    int  present;
};

struct hda_widget {
    u8    node_id;
    u8    type;
    u32   capabilities;
    u16   pcm;
    u16   stream;
    u32   pin_caps;
    u32   config_default;
    u8    amp_in_caps;
    u8    amp_out_caps;
    u16   connection_list;
};

struct hda_controller {
    u8    bus;
    u8    dev;
    u8    func;
    u32   bar0;
    void  *mmio;
    u16   gcap;
    u8    vmin;
    u8    vmaj;
    u16   outpay;
    u16   inpay;
    u32   *corb;
    u32   *rirb;
    u16   corb_wp;
    u16   rirb_wp;
    int   found;
};

int  hda_init(void);
void hda_codec_dump(struct hda_controller *ctl, struct hda_codec *codec);

#endif
