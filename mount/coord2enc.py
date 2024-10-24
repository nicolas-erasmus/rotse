# Purpose: Convert RA and DEC to encoder values for ROTSE
# Created: 22 Oct 2024 mostly by ChatGPT-4-turbo: guided by, modified, and tweaked by Nic Erasmus (SAAO)

import numpy as np
from scipy.interpolate import LinearNDInterpolator
from astropy.time import Time
from astropy.coordinates import EarthLocation, AltAz, SkyCoord
from astropy import units as u

# ROTSE location:
latitude = -23.272951  # ROTSE latitude from Google Maps
longitude = 16.502814  # ROTSE longitude from Google Maps
elevation = 1800       # Altitude from HESS Wikipedia

# Calibration points: [(HA, Dec), Encoder_X, Encoder_Y] in degrees
# calibration_data = [
#     ((-90.0, latitude + 45.0), 2309696, 1442022),
#     ((-90.0, latitude - 90.0), 2309696, -1195859),
#     ((90.0, latitude + 45.0), -2046480, 1442022),
#     ((90.0, latitude - 90.0), -2046480, -1195859)
# ]

# calibration_data = [
#     ((-69.997, -14.565), 1853086, 1299012),
#     ((70.912, -10.56),  -1582589, 1355807),
#     ((-2.389, 30.733), 194421, 2166675),
#     ((3.023, -83.108), 110055, -309384)
# ]

# calibration_data = [
#     ((-70.528, 6.372), 1752867, 1227690),
#     ((70.504, 7.347),  -1683431, 1227690),
#     ((-4.828, -84.399), 4526, -760272),
#     ((0.996, 25.721), 4526, 1592783)
# ]

calibration_data = [
    ((-47.450399016439576, -20.278), 1195510, 707781), 
    ((-47.702140239141585, 12.104),  1195510, 1335993),
    ((-32.05169058054409, 1.052), 816039, 1119519),
    ((-59.83958205731879, 0.886), 1493334, 1119519)
]

# Extract HA, Dec, and Encoder values for interpolation
ha_dec_points = np.array([point[0] for point in calibration_data])
encoder_x = np.array([point[1] for point in calibration_data])
encoder_y = np.array([point[2] for point in calibration_data])

# Create interpolators for X and Y encoders
encoder_x_interpolator = LinearNDInterpolator(ha_dec_points, encoder_x)
encoder_y_interpolator = LinearNDInterpolator(ha_dec_points, encoder_y)

def ra_dec_to_encoders(ra, dec, lst):
    """Convert RA and Dec to encoder values"""
    # Convert RA to HA (in degrees)
    ha = lst - ra 
    print(f"HA = {ha}")
    ha_dec = (ha, dec)
    
    # Interpolate encoder values
    encoder_x_val = encoder_x_interpolator(ha_dec)
    encoder_y_val = encoder_y_interpolator(ha_dec)
    
    return encoder_x_val, encoder_y_val

# Example usage
if __name__ == "__main__":
    
    observation_time = Time.now()# Time("2024-10-23T20:00:50")Time("2024-10-23T21:10:16")# # Example observation time (UTC)
    print(observation_time)
    
    lst = observation_time.sidereal_time('mean', longitude=longitude).deg #in degrees
    print(f"LST = {lst}")
    
    # Example RA and Dec (in degrees) and observation time
    ra = lst #in degrees
    dec = latitude #in degrees
    print(f"RA = {ra}, Dec = {dec}")
    
    
    encoder_x_val, encoder_y_val = ra_dec_to_encoders(ra, dec, lst)
    print(f"Encoder X: {encoder_x_val}, Encoder Y: {encoder_y_val}")
