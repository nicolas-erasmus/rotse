# Purpose: Convert RA and DEC to encoder values for ROTSE
# Converted to python using original ROTSE c-code written by Don Smith &  E. Rykoff
# Created: 21 Oct 2024 mostly by ChatGPT-4-turbo, guided by, modified, and tweaked by Nic Erasmus (SAAO)

import numpy as np
from astropy.coordinates import SkyCoord, EarthLocation
from astropy.coordinates import Angle
from astropy import units as u
from astropy.time import Time
import logging

# Configuration structure, which would hold necessary parameters
class MountConfig:
    def __init__(self, latitude, longitude, elevation, coomat, rarange, poleoff, deg2enc, zeropt, ptg_offset):
        self.latitude = latitude
        self.longitude = longitude
        self.elevation = elevation
        self.coomat = np.array(coomat)
        self.rarange = rarange
        self.poleoff = poleoff
        self.deg2enc = deg2enc
        self.zeropt = zeropt
        self.ptg_offset = ptg_offset

# Helper function to convert RA to HA
def ra_to_ha(ra, lst):
    ha = (lst - ra).wrap_at(24 * u.hour)
    return ha

# Helper function to convert HA, Dec to Cartesian coordinates
def ha_dec_to_xyz(ha, dec):
    cos_dec = np.cos(dec)
    x = np.cos(ha) * cos_dec
    y = np.sin(ha) * cos_dec
    z = np.sin(dec)
    return np.array([x, y, z])

# Helper function to convert Cartesian coordinates back to spherical (HA, Dec)
def xyz_to_spherical(xyz):
    x, y, z = xyz
    r = np.sqrt(x**2 + y**2)
    dec = np.arcsin(z)
    ha = np.arccos(x / r) if y >= 0 else 2 * np.pi - np.arccos(x / r)
    return ha, dec

# Function to apply the matrix and calculate encoder positions
def apply_matrix(ha, dec, cfg):
    logging.info("Applying pointing matrix...")

    # Convert HA, Dec to XYZ vector
    vector = ha_dec_to_xyz(ha, dec)

    # Apply the rotation matrix from the configuration
    rotated_vector = np.dot(cfg.coomat, vector)

    # Convert back to spherical coordinates (HA, Dec)
    ha_rotated, dec_rotated = xyz_to_spherical(rotated_vector)

    # Convert to degrees
    ha_rotated_deg = np.degrees(ha_rotated)
    dec_rotated_deg = np.degrees(dec_rotated)

    # Handle southern hemisphere if necessary
    if cfg.latitude < 0:
        ha_rotated_deg *= -1
        dec_rotated_deg *= -1

    # Apply offsets and adjust for pole offset
    dec_rotated_deg -= cfg.poleoff

    # Convert to encoder steps
    enc_ra = int(ha_rotated_deg * cfg.deg2enc[0]) + cfg.zeropt[0] + cfg.ptg_offset[0]
    enc_dec = int(dec_rotated_deg * cfg.deg2enc[1]) + cfg.zeropt[1] + cfg.ptg_offset[1]

    logging.info(f"Encoder positions: RA = {enc_ra}, Dec = {enc_dec}")
    return enc_ra, enc_dec

# Function to calculate HA and Dec from RA, Dec and time/location
def ra_dec_to_enc(ra, dec, cfg, observation_time):
    # Set up the observatory location
    location = EarthLocation(lat=cfg.latitude * u.deg, lon=cfg.longitude * u.deg, height=cfg.elevation * u.m)
    
    # Get the Local Sidereal Time (LST) at the observation time
    time = Time(observation_time)
    lst = time.sidereal_time('mean', longitude=cfg.longitude)

    # Convert RA to HA
    ha = ra_to_ha(Angle(ra, unit=u.hourangle), lst)

    # Call the apply_matrix function to compute encoder positions
    return apply_matrix(ha.rad, np.radians(dec), cfg)

# Example usage with dummy configuration
if __name__ == "__main__":
    # Example mount configuration with made-up values
    cfg = MountConfig(
        latitude=-23.272951, #rotse latitude from google maps
        longitude=16.502814, #rotse longitude from google maps
        elevation=1800,  #altitude form HESS wikipedia
        coomat=[[1, 0, 0], [0, 1, 0], [0, 0, 1]],  # Identity matrix for simplicity
        rarange=[0, 360],
        poleoff=0.5,
        deg2enc=[1000, 1000],  # Encoder ticks per degree (dummy values)
        zeropt=[0, 0],         # Zero point offsets
        ptg_offset=[10, 10]    # Pointing offsets
    )

    # Example RA and Dec (in degrees) and observation time
    ra = 10.684  # Example RA in hours (this is about the RA of Andromeda Galaxy)
    dec = 41.269  # Example Dec in degrees
    observation_time = "2024-10-21T22:00:00"  # Example observation time (UTC)

    # Convert RA, Dec to encoder positions
    enc_ra, enc_dec = ra_dec_to_enc(ra, dec, cfg, observation_time)

    print(f"Encoder positions: RA = {enc_ra}, Dec = {enc_dec}")
