"""Single, reviewable electrical source for TiltBox Voice A.

Pin numbers come from the named manufacturer's datasheet, never net-position
inference. Coordinates are mm, viewed from the LED side, relative to board centre.
Run with Python 3; build_board.py requires KiCad 9's bundled Python.
"""
import json, uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARTS = []
GPIO = {'WAKE_N':4,'IMU_INT_N':5,'I2C_SDA':6,'I2C_SCL':7,'BAT_ADC':8,
        'LED_FAULT_N':9,'BOOST_EN':10,'LED_EN':11,'LED_DATA_3V3':12,
        'MIC_WS':13,'MIC_BCLK':14,'MIC_SD':15,'MIC_EN':16,
        'CHG_STAT1':17,'CHG_STAT2':18,'USB_DM':19,'USB_DP':20,'VBUS_SENSE':21}
def uid(s): return str(uuid.uuid5(uuid.NAMESPACE_URL, 'tiltbox-voice-rev-a/'+s))
def add(ref,value,fp,lcsc,pins,xy,angle=0,side='B',sheet='power',names=None,types=None):
    p=dict(ref=ref,value=value,footprint=fp,lcsc=lcsc,pins={str(k):v for k,v in pins.items()},
           xy=list(xy),angle=angle,side=side,sheet=sheet,uuid=uid(ref),
           names={str(k):v for k,v in (names or {}).items()},
           types={str(k):v for k,v in (types or {}).items()})
    PARTS.append(p); return p
R_CODES={'10k':'C25744','100k':'C25741','4k7':'C25900','732k':'C27016','49k9':'C25897',
 '18k':'C25762','200k':'C25764','1M':'C26083','150k':'C25755','5k1':'C25905','33':'C25105',
 '22':'C25092','1k':'C11702'}
def r(n,val,a,b,xy,angle=0,sheet='power'):
    return add('R'+str(n),val,'Resistor_SMD:R_0402_1005Metric',R_CODES[val],{1:a,2:b},xy,angle,sheet=sheet)
def c(n,val,a,b,xy,angle=0,side='B',sheet='power'):
    size,code={'100n':('0402','C1525'),'1u':('0603','C15849'),'10u':('0603','C19702'),'22u':('0805','C45783')}[val]
    metric={'0402':'1005','0603':'1608','0805':'2012'}[size]
    return add('C'+str(n),val,'Capacitor_SMD:C_'+size+'_'+metric+'Metric',code,{1:a,2:b},xy,angle,side,sheet)

# USB-C input; 5.1k Rd on each CC pin. Shells and all redundant contacts are wired.
usb={x:'GND' for x in ['A1','A12','B1','B12','S1']}
usb.update({x:'VBUS' for x in ['A4','A9','B4','B9']})
usb.update({'A5':'CC1','B5':'CC2','A6':'USB_CON_DP','B6':'USB_CON_DP',
            'A7':'USB_CON_DM','B7':'USB_CON_DM','A8':None,'B8':None})
CONTROL_X = 36.51917 / 2  # Midpoints between the three original +Y screw axes.
USB_ECO_DX = CONTROL_X - 25
add('J1','TYPE-C 16PIN 2MD(073)','Connector_USB:USB_C_Receptacle_HCTL_HC-TYPE-C-16P-01A','C2765186',usb,(CONTROL_X,-36.0),0)
add('J2','S2B-PH-SM4-TB(LF)(SN)','Connector_JST:JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal','C295747',
    {1:'VBAT',2:'GND','MP':'GND'},(-27,-14),90)
r(1,'5k1','CC1','GND',(32+USB_ECO_DX,-36),90)
r(2,'5k1','CC2','GND',(34+USB_ECO_DX,-33),90)
add('D65','USBLC6-2SC6','Package_TO_SOT_SMD:SOT-23-6','C2687116',
    {1:'USB_CON_DP',2:'GND',3:'USB_CON_DM',4:'USB_CON_DM',5:'VBUS',6:'USB_CON_DP'},(16+USB_ECO_DX,-34),0)
c(65,'1u','VBUS','GND',(29.5,-36),90)
r(3,'22','USB_CON_DM','USB_DM',(1.5,-26.98),0,sheet='logic')
r(4,'22','USB_CON_DP','USB_DP',(1.5,-28.25),0,sheet='logic')

# Power-path charger. ILIM 500 mA / 4.2 V cell; ICHG 300 mA.
add('U2','BQ25185DLHR','Package_DFN_QFN:Texas_DLH0010A_WSON-10-1EP_2.2x2mm_P0.4mm_EP0.9x1.5mm','C19725033',
    {1:'VSYS',2:'VBAT',3:'CHG_STAT2',4:'GND',5:'GND',6:'BAT_NTC',7:'CHG_ILIM',8:'CHG_ISET',9:'CHG_STAT1',10:'VBUS',11:'GND'},(-22,-25),
    names={1:'SYS',2:'BAT',3:'STAT2',4:'CE_N',5:'GND',6:'TS_MR',7:'ILIM_VSET',8:'ISET',9:'STAT1',10:'IN',11:'EP'},
    types={1:'power_out',2:'power_in',3:'open_collector',4:'input',5:'power_in',6:'input',7:'passive',8:'passive',9:'open_collector',10:'power_in',11:'passive'})
c(66,'1u','VBUS','GND',(-18.5,-22),0)
c(67,'22u','VSYS','GND',(-26,-21.5),180)
c(68,'1u','VBAT','GND',(-26,-25),180)
r(5,'18k','CHG_ILIM','GND',(-18.5,-26.5),0)
r(6,'1k','CHG_ISET','GND',(-19,-28),0)
r(7,'10k','3V3','CHG_STAT1',(-18,-19.5),0)
r(8,'10k','3V3','CHG_STAT2',(-21,-19.5),0)
add('TH1','SDNT1608X103F3435FTF','Resistor_SMD:R_0603_1608Metric','C525660',{1:'BAT_NTC',2:'GND'},(-8,18),sheet='power')

# 3.3 V is buck-boost, not an LDO: the entire usable single-cell range is supported.
add('U3','TPS63031DSKR','Package_SON:WSON-10-1EP_2.5x2.5mm_P0.5mm_EP1.2x2mm','C15516',
    {1:'3V3',2:'BUCK_L2',3:'GND',4:'BUCK_L1',5:'VSYS',6:'VSYS',7:'GND',8:'VSYS',9:'GND',10:'3V3',11:'GND'},(-9,-25),
    names={1:'VOUT',2:'L2',3:'PGND',4:'L1',5:'VIN',6:'EN',7:'PS_SYNC',8:'VINA',9:'GND',10:'FB',11:'EP'},
    types={1:'power_out',2:'passive',3:'power_in',4:'passive',5:'power_in',6:'input',7:'input',8:'power_in',9:'power_in',10:'input',11:'passive'})
add('L1','FNR4018S1R5MT','Inductor_SMD:L_Changjiang_FNR4018S','C167802',{1:'BUCK_L1',2:'BUCK_L2'},(-14.5,-25),270)
c(69,'22u','VSYS','GND',(-10,-30),90)
c(70,'100n','VSYS','GND',(-8.5,-22.2),0)
c(71,'22u','3V3','GND',(-10,-20),270)
c(72,'22u','3V3','GND',(-6,-20),270)

# 5 V true-disconnect boost plus separate latched-off current limiter.
add('U4','TPS61023DRLR','Package_TO_SOT_SMD:SOT-563','C919459',
    {1:'BOOST_FB',2:'BOOST_EN',3:'VSYS',4:'GND',5:'BOOST_SW',6:'5V_RAW'},(-23,0),
    names={1:'FB',2:'EN',3:'VIN',4:'GND',5:'SW',6:'VOUT'},
    types={1:'input',2:'input',3:'power_in',4:'power_in',5:'passive',6:'power_out'})
add('L2','FNR4018S1R0MT','Inductor_SMD:L_Changjiang_FNR4018S','C167801',{1:'VSYS',2:'BOOST_SW'},(-24,-4.5),0)
c(73,'22u','VSYS','GND',(-28.5,-3.4),90)
c(74,'22u','VSYS','GND',(-28.5,.2),90)
c(75,'22u','5V_RAW','GND',(-18,-1.4),0)
c(76,'22u','5V_RAW','GND',(-18,1.4),0)
r(9,'732k','5V_RAW','BOOST_FB',(-25.5,2.4),90)
r(10,'100k','BOOST_FB','GND',(-23.8,2.4),90)
r(11,'100k','BOOST_EN','GND',(-25.5,5),0)
add('U5','TPS2553DBVR-1','Package_TO_SOT_SMD:SOT-23-6','C111738',
    {1:'5V_RAW',2:'GND',3:'LED_EN',4:'LED_FAULT_N',5:'LED_ILIM',6:'LED_5V'},(-17,7),
    names={1:'IN',2:'GND',3:'EN',4:'FAULT_N',5:'ILIM',6:'OUT'},
    types={1:'power_in',2:'power_in',3:'input',4:'open_collector',5:'passive',6:'power_out'})
r(12,'49k9','LED_ILIM','GND',(-14,7),90)
r(13,'100k','LED_EN','GND',(-20,8),90)
r(14,'10k','3V3','LED_FAULT_N',(-14,10),0)
c(77,'1u','LED_5V','GND',(-17,11),0)
add('U6','SN74LVC2T45DCUR','Package_SO:VSSOP-8_2.3x2mm_P0.5mm','C15741',
    {1:'3V3',2:'LED_DATA_3V3',3:'GND',4:'GND',5:'3V3',6:None,7:'LED_DATA_5V',8:'LED_5V'},(-18,16),
    names={1:'VCCA',2:'A1',3:'A2',4:'GND',5:'DIR',6:'B2',7:'B1',8:'VCCB'},
    types={1:'power_in',2:'input',3:'input',4:'power_in',5:'input',6:'output',7:'output',8:'power_in'})
c(78,'100n','3V3','GND',(-21,14.5),90)
c(79,'100n','LED_5V','GND',(-15,14.5),90)
r(15,'33','LED_DATA_5V','LED_00',(-14,18),0)
r(16,'100k','LED_DATA_3V3','GND',(-22,17),90)

# MCU module. 35/36/37 reserved for octal PSRAM and explicitly NC.
esp_gpio={4:4,5:5,6:6,7:7,8:15,9:16,10:17,11:18,12:8,13:19,14:20,15:3,16:46,17:9,18:10,
 19:11,20:12,21:13,22:14,23:21,24:47,25:48,26:45,27:0,28:35,29:36,30:37,31:38,
 32:39,33:40,34:41,35:42,36:44,37:43,38:2,39:1}
pinout={k:next((n for n,g in GPIO.items() if g==gpio), None) for k,gpio in esp_gpio.items()}
pinout.update({1:'GND',2:'3V3',3:'ESP_EN',27:'BOOT_N',36:'UART_RX',37:'UART_TX',40:'GND',41:'GND'})
add('U1','ESP32-S3-WROOM-1U-N16R8','TiltBoxA:ESP32-S3-WROOM-1U','C3013946',pinout,(13,-20),0,sheet='logic',
    names={**{k:'GPIO'+str(g) for k,g in esp_gpio.items()},1:'GND',2:'3V3',3:'EN',40:'GND',41:'EP_GND'},
    types={**{k:'bidirectional' for k in esp_gpio},1:'power_in',2:'power_in',3:'input',40:'passive',41:'passive'})
c(80,'22u','3V3','GND',(.5,-13),90,sheet='logic')
c(81,'100n','3V3','GND',(2,-13),90,sheet='logic')
r(17,'10k','3V3','ESP_EN',(.5,-16.5),0,sheet='logic')
c(82,'1u','ESP_EN','GND',(.5,-18.5),0,sheet='logic')
r(18,'10k','3V3','BOOT_N',(6,-7),0,sheet='logic')
add('SW2','TS-1187A-B-A-B','Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A','C318884',{1:'ESP_EN',2:'GND'},(29,-8),sheet='logic')
add('SW3','TS-1187A-B-A-B','Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A','C318884',{1:'BOOT_N',2:'GND'},(29,0),sheet='logic')
add('SW1','TS-1101VS-C-A-A','TiltBoxA:KEY-SMD_TS-1101VS-C-A-A','C318936',
    {1:'WAKE_N',2:'GND',3:'GND',4:'GND'},(-CONTROL_X,-36.5),180,sheet='logic')
r(19,'100k','3V3','WAKE_N',(31,-12),0,sheet='logic')
c(83,'100n','WAKE_N','GND',(34,-12),0,sheet='logic')
# A.3: 1 MOhm divider, 2.1 uA at 4.2 V; C84 gives a nominal 50 ms time constant.
r(20,'1M','VBAT','BAT_ADC',(1,-9),90,sheet='logic')
r(21,'1M','BAT_ADC','GND',(-1,-9),90,sheet='logic')
c(84,'100n','BAT_ADC','GND',(-3,-9),90,sheet='logic')
r(22,'100k','VBUS','VBUS_SENSE',(7,-4),0,sheet='logic')
r(23,'150k','VBUS_SENSE','GND',(10,-4),0,sheet='logic')

# IMU remains supplied in sleep. Firmware polls it; only the button wakes the ESP.
# CTRL3_C.H_LACTIVE=1 keeps unused push-pull INT1 high, avoiding DC loss in R26.
add('U7','LSM6DS3TR-C','Package_LGA:LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y','C967633',
    {1:'GND',2:'GND',3:'GND',4:'IMU_INT_N',5:'3V3',6:'GND',7:'GND',8:'3V3',9:None,10:None,11:None,12:'3V3',13:'I2C_SCL',14:'I2C_SDA'},(0,0),sheet='sensors',
    names={1:'SA0',2:'SDx',3:'SCx',4:'INT1',5:'VDDIO',6:'GND',7:'GND',8:'VDD',9:'INT2',10:'NC',11:'NC',12:'CS_N',13:'SCL',14:'SDA'},
    types={1:'input',2:'input',3:'input',4:'output',5:'power_in',6:'power_in',7:'power_in',8:'power_in',9:'output',10:'no_connect',11:'no_connect',12:'input',13:'input',14:'bidirectional'})
c(85,'100n','3V3','GND',(-2.8,0.5),90,sheet='sensors')
c(86,'100n','3V3','GND',(2.8,0.5),90,sheet='sensors')
c(87,'1u','3V3','GND',(0,4),0,sheet='sensors')
r(24,'4k7','3V3','I2C_SCL',(-4,-3),0,sheet='sensors')
r(25,'4k7','3V3','I2C_SDA',(-1,-3),0,sheet='sensors')
r(26,'100k','3V3','IMU_INT_N',(2,-3),0,sheet='sensors')
add('U8','ICS-43434','Sensor_Audio:InvenSense_ICS-43434-6_3.5x2.65mm','C5656610',
    {1:'MIC_WS',2:'GND',3:'GND',4:'MIC_BCLK',5:'MIC_3V3',6:'MIC_SD'},(35,22),0,'F','sensors',
    names={1:'WS',2:'LR',3:'GND',4:'SCK',5:'VDD',6:'SD'},
    types={1:'input',2:'input',3:'power_in',4:'input',5:'power_in',6:'tri_state'})
add('U9','TPS22918DBVR','Package_TO_SOT_SMD:SOT-23-6','C131941',
    {1:'3V3',2:'GND',3:'MIC_EN',4:None,5:'MIC_3V3',6:'MIC_3V3'},(29,18),sheet='sensors',
    names={1:'VIN',2:'GND',3:'ON',4:'CT',5:'QOD',6:'VOUT'},
    types={1:'power_in',2:'power_in',3:'input',4:'passive',5:'passive',6:'power_out'})
r(27,'100k','MIC_EN','GND',(26,18),90,sheet='sensors')
r(28,'100k','MIC_SD','GND',(34,25),0,sheet='sensors')
c(88,'1u','3V3','GND',(29,14),0,sheet='sensors')
c(89,'1u','MIC_3V3','GND',(29,22),0,sheet='sensors')
c(90,'100n','MIC_3V3','GND',(35,19.5),0,'F','sensors')

# Original enclosure grid: 58.3 mm first-to-last centres, 8 equal steps.
PITCH=58.3/7
for row in range(8):
    for chaincol in range(8):
        col=chaincol if row%2==0 else 7-chaincol
        i=row*8+chaincol
        xy=(-29.15+col*PITCH,-29.15+row*PITCH)
        add('D'+str(i+1),'WS2812B-B/T (V5)','LED_SMD:LED_WS2812B_PLCC4_5.0x5.0mm_P3.2mm','C2761795',
            {1:'LED_5V',2:'LED_%02d'%(i+1),3:'GND',4:'LED_%02d'%i},xy,180 if row%2==0 else 0,'F','leds_1' if i<32 else 'leds_2',
            names={1:'VDD',2:'DOUT',3:'GND',4:'DIN'},types={1:'power_in',2:'output',3:'power_in',4:'input'})
        # Revision A.1: C1..C64 deliberately omitted. The exact WS2812B V5
        # datasheet (C2761795, p. 4) requires no external per-pixel capacitor.
        # Keep references C65 and above stable for all remaining circuitry.

for n,(net,xy) in enumerate([
 ('GND',(-31,28)),('VBAT',(-31,24)),('VSYS',(-31,20)),('3V3',(-31,16)),('5V_RAW',(-31,12)),
 ('LED_5V',(-31,8)),('UART_TX',(10,5)),('UART_RX',(13,5)),('ESP_EN',(16,5)),('BOOT_N',(19,5)),
 ('LED_64',(34,31)),('BAT_NTC',(-11,18)),('I2C_SDA',(-7,2)),('I2C_SCL',(-7,5)),('MIC_SD',(30,27))],1):
    add('TP'+str(n),net,'TestPoint:TestPoint_Pad_D1.5mm','',{1:net},xy,sheet='sensors')
for i,xy in enumerate([(0,-36.51917),(36.51917,0),(0,36.51917),(-36.51917,0)]+[(sx*36.51917,sy*36.51917) for sx in [-1,1] for sy in [-1,1]],1):
    add('H'+str(i),'M3 / NPTH 3.2','MountingHole:MountingHole_3.2mm_M3','',{},xy,side='F',sheet='mechanical')

def save():
    ROOT.mkdir(parents=True,exist_ok=True)
    (ROOT/'design.json').write_text(json.dumps({'revision':'A.3','board_mm':78,'thickness_mm':1.6,'grid_pitch_mm':PITCH,'gpio':GPIO,'parts':PARTS},indent=2)+'\n')
if __name__=='__main__': save()
