# Purpose: A little command line tool to control the L12 Actuonix Motion Devices
# Created: 20 Oct 2024 by  ChatGPT-4-turbo, modified and tweaked by Nic Erasmus (SAAO)

import time
import keyboard  # To detect keypresses
from actuonix_lac.lac import LAC

class ActuatorControlTool:
    def __init__(self):
        # Initialize the LAC actuator
        self.actuator = LAC()
        self.stroke = 10.0  # The stroke range of the actuator in mm (for L12)

    def mm_to_units(self, mm):
        return int((mm / self.stroke) * 1023)

    def units_to_mm(self, units):
        return (units / 1023) * self.stroke

    def set_accuracy(self):
        value = float(input("Enter accuracy in mm (e.g., 0.1): "))  # Use float for decimal values
        units = self.mm_to_units(value)
        self.actuator.set_accuracy(units)
        print(f"Accuracy set to {value} mm")

    def set_retract_limit(self):
        value = float(input("Enter retract limit in mm: "))  # Allow non-integer inputs
        units = self.mm_to_units(value)
        self.actuator.set_retract_limit(units)
        print(f"Retract limit set to {value} mm")

    def set_extend_limit(self):
        value = float(input("Enter extend limit in mm: "))  # Allow non-integer inputs
        units = self.mm_to_units(value)
        self.actuator.set_extend_limit(units)
        print(f"Extend limit set to {value} mm")

    def set_speed(self):
        value = float(input("Enter speed in mm/s (max 10 mm/s): "))  # Allow non-integer inputs
        if value > 10.0:
            print("Speed cannot exceed 10 mm/s. Setting speed to 10 mm/s.")
            value = 10.0
        units = self.mm_to_units(value)
        self.actuator.set_speed(units)
        print(f"Speed set to {value} mm/s")

    def set_position(self):
        value = float(input("Enter position in mm: "))  # Allow non-integer inputs
        units = self.mm_to_units(value)
        self.actuator.set_position(units)
        print(f"Position set to {value} mm")
        
    def show_position(self):
        current_position = self.actuator.get_feedback()
        print(f"Current position: {self.units_to_mm(current_position):.2f} mm")    

    def continuous_control(self):
        print("Press left arrow to retract and right arrow to extend. Press 'q' to quit.")
        current_position = self.actuator.get_feedback()
        
        while True:
            if keyboard.is_pressed('left'):
                current_position -= self.mm_to_units(0.05)  # Retract by 0.1mm
                if current_position < 0:
                    current_position = 0
                self.actuator.set_position(current_position)
                time.sleep(0.1)

            elif keyboard.is_pressed('right'):
                current_position += self.mm_to_units(0.05)  # Extend by 0.1mm
                if current_position > self.mm_to_units(self.stroke):
                    current_position = self.mm_to_units(self.stroke)
                self.actuator.set_position(current_position)
                time.sleep(0.1)

            elif keyboard.is_pressed('q'):
                print("Exiting continuous control.")
                break

            time.sleep(0.1)

    def menu(self):
        while True:
            print("\nActuator Control Menu:")
            print("1. Set accuracy")
            print("2. Set retract limit")
            print("3. Set extend limit")
            print("4. Set speed")
            print("5. Move to specific position")
            print("6. Show current position")  # New menu item
            print("7. Continuous control (Use arrow keys)") 
            print("0. Exit")

            choice = input("Select an option: ")

            if choice == '1':
                self.set_accuracy()
            elif choice == '2':
                self.set_retract_limit()
            elif choice == '3':
                self.set_extend_limit()
            elif choice == '4':
                self.set_speed()
            elif choice == '5':
                self.set_position()
            elif choice == '6':
                self.show_position()
            elif choice == '7':
                self.continuous_control() 
            elif choice == '0':
                print("Exiting...")
                break
            else:
                print("Invalid choice, please try again.")

if __name__ == "__main__":
    tool = ActuatorControlTool()
    tool.menu()
