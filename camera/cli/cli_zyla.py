# Purpose: A little command line tool to control the Zyla camera
# Created: 20 Oct 2024 by  ChatGPT-4-turbo using base code developed by Enzo Afonso (UCT/SAAO), modified and tweaked by Nic Erasmus (SAAO)

from atcore import *  # import the python wrapper for the driver
import math
import numpy as np
import time
#import matplotlib
from matplotlib import pyplot as plt
#matplotlib.use('Agg')  # Use non-interactive backend for headless environments
import astropy.visualization
import os
from astropy.io import fits
from datetime import datetime

# Initialize Andor SDK3
def initialize_camera():
    print("Initializing Andor's SDK3...")
    andor_driver = ATCore()
    zyla_camera = andor_driver.open(0)
    print(
        f'Connected to camera {andor_driver.get_string(zyla_camera, "CameraModel")} with serial number: {andor_driver.get_string(zyla_camera, "SerialNumber")}'
    )

    # Turn off cooling on initialization
    andor_driver.set_bool(zyla_camera, "SensorCooling", False)
    return andor_driver, zyla_camera

# Display current camera settings including cooling and exposure time
def print_all(andor_driver, zyla_camera, cooling, exposure_time):
    full_frame = (
        0,
        0,
        andor_driver.get_int(zyla_camera, "SensorWidth"),
        andor_driver.get_int(zyla_camera, "SensorHeight"),
    )

    window = (
        andor_driver.get_int(zyla_camera, "AOILeft") - 1,
        andor_driver.get_int(zyla_camera, "AOITop") - 1,
        andor_driver.get_int(zyla_camera, "AOIWidth"),
        andor_driver.get_int(zyla_camera, "AOIHeight"),
    )

    binning = _binning_string_to_tuple(
        andor_driver.get_enum_string(zyla_camera, "AOIBinning")
    )

    print(f"Full frame: {full_frame}")
    print(f"Window: {window}")
    print(f"Binning: {binning}")
    print(f"Cooling: {'On' if cooling else 'Off'}")
    print(f"Exposure Time: {exposure_time} seconds")

# Convert binning string to tuple
def _binning_string_to_tuple(bin):
    a, b = bin.split("x")
    return int(a), int(b)

# Function to capture and display an image, optionally save it as a FITS file
def capture_image(andor_driver, zyla_camera, window, binning, exposure_time, cooling, ax=None, save_image=True):
    width = int(math.floor(window[2]) / binning)
    height = int(math.floor(window[3]) / binning)

    andor_driver.set_enum_string(zyla_camera, "AOIBinning", f"{binning}x{binning}")
    andor_driver.set_int(zyla_camera, "AOIWidth", width)
    andor_driver.set_int(zyla_camera, "AOIHeight", height)
    andor_driver.set_int(zyla_camera, "AOILeft", window[0] + 1)
    andor_driver.set_int(zyla_camera, "AOITop", window[1] + 1)

    # Set exposure time
    andor_driver.set_float(zyla_camera, "ExposureTime", exposure_time)

    # Cooling already set in the main menu
    andor_driver.set_enum_string(zyla_camera, "PixelEncoding", "Mono16")
    imageSizeBytes = andor_driver.get_int(zyla_camera, "ImageSizeBytes")

    buf = np.empty((imageSizeBytes,), dtype="B")
    andor_driver.queue_buffer(zyla_camera, buf.ctypes.data, imageSizeBytes)

    andor_driver.command(zyla_camera, "AcquisitionStart")

    print(f"Exposing sensor for {exposure_time} seconds...")
    while True:
        time.sleep(0.1)
        try:
            andor_driver.wait_buffer(zyla_camera, timeout=0)
            andor_driver.command(zyla_camera, "AcquisitionStop")
            break
        except:
            pass

    print("Exposure complete, reading out...")

    aoistride = andor_driver.get_int(zyla_camera, "AOIStride")

    np_arr = buf[0 : height * aoistride]
    np_d = np_arr.view(dtype=np.uint16)
    np_d = np_d.reshape(height, round(np_d.size / height))
    formatted_img = np_d[0:height, 0:width]
    
    # Check if all pixels have the same value
    if np.all(formatted_img == formatted_img[0, 0]):
        print("Warning: All pixels have the same value. Check camera settings or data acquisition process.")

    # Display the image using astropy ZScaleInterval
    zscaler = astropy.visualization.ZScaleInterval()
    if ax is not None:
        ax.clear()  # Clear the previous image
        ax.imshow(
            formatted_img,
            cmap="gray",
            vmin=zscaler.get_limits(formatted_img)[0],
            vmax=zscaler.get_limits(formatted_img)[1],
        )
        plt.draw()  # Update the figure with the new image
    else:
        fig, ax = plt.subplots(1, 1)
        ax.imshow(
            formatted_img,
            cmap="gray",
            vmin=zscaler.get_limits(formatted_img)[0],
            vmax=zscaler.get_limits(formatted_img)[1],
        )
        plt.show()
        plt.close(fig)  # Close the figure after display

    # Save the image as a FITS file only if save_image is True
    if save_image:
        save_image_as_fits(formatted_img)

# Function for continuous video mode (no image saving)
def video_mode(andor_driver, zyla_camera, window, binning, exposure_time, cooling):
    try:
        print("Entering video mode. Press Ctrl+C to stop.")
        
        plt.ion()  # Enable interactive mode for continuous updating
        fig, ax = plt.subplots(1, 1)
        
        while True:
            capture_image(andor_driver, zyla_camera, window, binning, exposure_time, cooling, ax, save_image=False)
            plt.pause(0.1)  # Allow the plot to update in interactive mode

    except KeyboardInterrupt:
        print("Exiting video mode...")
        plt.close(fig)  # Ensure the figure is closed to avoid tkinter errors
        plt.ioff()  # Turn off interactive mode

# Fix for filename incrementing issue
def save_image_as_fits(image_data):
    # Generate directory and file name based on current date
    date_str = datetime.now().strftime("%Y%m%d")
    base_dir = os.getcwd() + f"/data/{date_str}"
    if not os.path.exists(base_dir):
        os.makedirs(base_dir)

    # Find the next available file number (0001, 0002, etc.)
    existing_files = sorted([f for f in os.listdir(base_dir) if f.startswith(f"rotse_{date_str}") and f.endswith(".fits")])
    if existing_files:
        last_file = existing_files[-1]
        next_num = int(last_file.split('_')[1].split('.')[0]) + 1  # Fixes the date duplication issue
    else:
        next_num = 1
    file_num = f"{next_num:04d}"

    # Correct the filename format
    fits_filename = f"{base_dir}/rotse_{date_str}.{file_num}.fits"
    
    # Save the image data to a FITS file
    hdu = fits.PrimaryHDU(image_data)
    hdu.writeto(fits_filename, overwrite=True)
    print(f"Image saved to {fits_filename}")


# Main menu
def main_menu(andor_driver, zyla_camera):
    window = (0, 0, 2560, 2160)
    binning = 1
    exposure_time = 0.5
    cooling = 0

    while True:
        print("\nAndor Camera Control Menu:")
        print("1. Set Window")
        print("2. Set Binning")
        print("3. Set Exposure Time")
        print("4. Set Cooling (0 = Off, 1 = On)")
        print("5. Capture Image")
        print("6. Start Video Mode")  
        print("7. Show Current Settings")  
        print("8. Exit")
        
        choice = input("Enter your choice: ")

        if choice == "1":
            window = tuple(map(int, input("Enter window as 'x_start, y_start, width, height': ").split(",")))
        elif choice == "2":
            binning = int(input("Enter binning (1, 2, etc.): "))
        elif choice == "3":
            exposure_time = float(input("Enter exposure time in seconds: "))
        elif choice == "4":
            cooling = int(input("Enter cooling (0 = Off, 1 = On): "))
            andor_driver.set_bool(zyla_camera, "SensorCooling", cooling)
            if cooling == 1:
                print("Cooling sensor for 60 seconds...")
                time.sleep(60)
        elif choice == "5":
            capture_image(andor_driver, zyla_camera, window, binning, exposure_time, cooling)
        elif choice == "6":
            video_mode(andor_driver, zyla_camera, window, binning, exposure_time, cooling)
        elif choice == "7":
            print_all(andor_driver, zyla_camera, cooling, exposure_time)
        elif choice == "8":
            print("Exiting...")
            andor_driver.close(zyla_camera)
            break
        else:
            print("Invalid choice. Please try again.")

# Initialize and run the CLI
if __name__ == "__main__":
    andor_driver, zyla_camera = initialize_camera()
    main_menu(andor_driver, zyla_camera)
