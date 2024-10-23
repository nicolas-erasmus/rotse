# Purpose: A little command line tool to control the ROTSE mount
# Created: 23 Oct 2024 by ChatGPT-4-turbo, modified and tweaked by Nic Erasmus (SAAO)

from coord2enc import ra_dec_to_encoders
from send_commands import send_command
from astropy.time import Time
import time
import serial  # Ensure to import the serial module

# ROTSE location
longitude = 16.502814

# Check serial port settings
ser = serial.Serial(port='/dev/ttyS0',    # Use /dev/ttyS* for Linux, COM* for Windows
                    baudrate=9600,
                    timeout=1
                    )

def hex_to_degrees(hex_value):
    """Convert hexadecimal to degrees."""
    return float.fromhex(hex_value)

def goto_ra_dec(encoder_x_val, encoder_y_val):
    """Send commands to move to the given encoder positions."""
    send_command(ser, f"$PosRA {encoder_x_val}")
    time.sleep(0.1)  # Add 0.1s delay between commands
    send_command(ser, f"$PosDec {encoder_y_val}")
    time.sleep(0.1)  # Add 0.1s delay between commands
    send_command(ser, "$RunRA")
    time.sleep(0.1)  # Add 0.1s delay between commands
    send_command(ser, "$RunDec")
    time.sleep(0.1)  # Add 0.1s delay between commands

def halt_drives():
    """Halt both RA and Dec drives."""
    send_command(ser, "$HaltRA")
    time.sleep(0.1)  # Add 0.1s delay between commands
    send_command(ser, "$HaltDec")
    time.sleep(0.1)  # Add 0.1s delay between commands

def home_mount():
    """Home the mount (RA and Dec)."""
    send_command(ser, "$HomeRA")
    time.sleep(0.1)  # Add 0.1s delay between commands
    send_command(ser, "$HomeDec")
    time.sleep(0.1)  # Add 0.1s delay between commands

def display_menu():
    """Display the menu and process user input."""
    while True:
        print("\nROTSE Telescope Control Menu:")
        print("1. Goto RA/DEC")
        print("2. Halt drives")
        print("3. Home the mount")
        print("4. Exit")

        choice = input("Select an option (1-4): ")

        if choice == '1':
            ra_hex = input("Enter RA (in hexadecimal): ")
            dec_hex = input("Enter Dec (in hexadecimal): ")

            # Convert hexadecimal to degrees
            ra = hex_to_degrees(ra_hex)
            dec = hex_to_degrees(dec_hex)
            print(f"Converted RA = {ra} degrees, Dec = {dec} degrees")
            
            # Get the current sidereal time (LST)
            observation_time = Time.now()
            lst = observation_time.sidereal_time('mean', longitude=longitude).deg
            
            # Convert RA/Dec to encoder values
            encoder_x_val, encoder_y_val = ra_dec_to_encoders(ra, dec, lst)
            if encoder_x_val is not None and encoder_y_val is not None:
                # Send goto RA/DEC commands
                goto_ra_dec(encoder_x_val, encoder_y_val)
            else:
                print("Error: Invalid RA/DEC values or interpolation failure.")
        elif choice == '2':
            halt_drives()
        elif choice == '3':
            home_mount()
        elif choice == '4':
            print("Exiting...")
            break
        else:
            print("Invalid option. Please try again.")

if __name__ == "__main__":
    display_menu()
