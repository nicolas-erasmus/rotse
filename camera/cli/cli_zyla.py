from atcore import *  # import the python wrapper for the driver
import math
import numpy as np
import time
#import matplotlib
from matplotlib import pyplot as plt
#matplotlib.use('Agg')  # Use non-interactive backend for headless environments
import astropy.visualization

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

# Function to capture and display an image
def capture_image(andor_driver, zyla_camera, window, binning, exposure_time, cooling, ax=None):
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

    # Use astropy ZScaleInterval for scaling and display with matplotlib
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
    
# Function for continuous video mode
def video_mode(andor_driver, zyla_camera, window, binning, exposure_time, cooling):
    try:
        print("Entering video mode. Press Ctrl+C to stop.")
        
        # Prepare to display the first image
        fig, ax = plt.subplots(1, 1)
        
        while True:
            capture_image(andor_driver, zyla_camera, window, binning, exposure_time, cooling, ax)
            time.sleep(0.1)  # Short pause between frames for continuous capture
    except KeyboardInterrupt:
        print("Video mode stopped.")

# Main menu
def main_menu(andor_driver, zyla_camera):
    window = (0, 0, 1000, 1000)
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
