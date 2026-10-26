"""Shared nominal A.4 interface dimensions (mm, installed world coordinates)."""
from shapely.geometry import box
from design import CONTROL_X

USB_Z = 8.435  # Centre of the placed J1 metal mouth, not its mounting tabs.
USB_OUTER = (12.0, 6.3, 1.5)
USB_INNER = (9.34, 3.56, 1.2)  # >=0.20 mm from actual shell section at Y39.5.
USB_COLLAR_Y = (39.05, 40.05)

def rounded_rect(width, height, radius, x=0, y=0):
    return box(x-width/2+radius,y-height/2+radius,
               x+width/2-radius,y+height/2-radius).buffer(radius,resolution=48)

def usb_profile(inner=False):
    return rounded_rect(*(USB_INNER if inner else USB_OUTER),x=CONTROL_X,y=USB_Z)
