# ESP32 Project - Access Control
This project was created based on the ESP32 microcontroller.

## Requirements
 - Install `Arduino IDE`<br />
  You can download and install the `Arduino IDE` using this [link](https://www.arduino.cc/en/main/software).

## Run Project

- _Clone Repository_
  - git clone https://github.com/Universal-Access-Control/device.git

- _Open project's directory_

- _Open `AccessControl.ino` file using `Arduino IDE`_

- _Add Board Urls_
  - Open `Files -> Preferences`
  - In `Settings` tab, copy lines below to `Additional Bords Manager URLs`:
    ```
      https://dl.espressif.com/dl/package_esp32_index.json,
      http://arduino.esp8266.com/stable/package_esp8266com_index.json
    ```
    ![ESP32 URLs](/img/esp32-urls.png)
  - Press `OK` button

- _Install Board packages_
  - Make sure that you are connecting to the internet
  - Open `Tools -> Board:"<NAME_OF_BOARD>" -> Board Managers`
  - Type `ESP32` in search box
  - Click on install button <br />
    ![Install Packages](/img/install-packages.png)
  - If packages installed successfully you can close the `Board Manger`

- _Connect your board to computer using micro USB cable_

- _Select Board_
  - From `Tools -> Board:"<NAME_OF_BOARD>` menu, select `ESP32 Dev Module`

- _Select Port_
  - From `Tools -> Port`, select your device's connected port.


- _Upload Project_
  - Click on `Upload` button or Press `ctrl + U`

Congratulations!