# Evaluating Rule-Based vs. ML Confirmation for Interrupt-Driven Embedded Fall Detection (Pico W + BMA530)

This project implements and evaluates 2 different interrupt based, embedded fall detection systems. The goal of these systems is to be low power and allow for easy integration into other real-time embedded systems while maintaining maximum fall detection accuracy. 

Thus, these two fall detection systems are interrupt driven (non-polling) to free up the MCU as much as possible for other tasks or low-power operation.

The first system is purely rule-based, working off a state machine which is updated by accelerometer interrupts. The second system uses an accelerometer interrupt to detect free-fall, and then confirms if the event was a fall by passing the accelerometer's FIFO buffer into an pretrained ML model.

The following programs are included in this project:

**Fall Detection Firmware**:

- Rule-based fall detection firmware (freefall + impact logic)
- Machine Learning (Random Forest) based fall detection firmware

**Data Collection**:

- Fall data transmission firmware & TCP server receiver 

**Data Tools**

- Batch data parsing (raw collection files to CSV)
- Batch data graphing (CSV to matplotlib PNG)
- Batch feature extraction and dataset splitting (training/testing split + feature extraction)
- Machine Learning model training and testing (Random Forest)
- Evaluation tools (confusion matrix, metrics)
- Comparison between real and simulated fall data

---

## Project Structure

- **analysis/**: Data & Model Analysis
- **data/**: Training Data, Testing Data and Graphs
- **firmware/**: firmware for fall detect implementation and  data collection 
- **tools/**: Various python scripts for data parsing, evaluation, ML training, etc.
- **README.md**: This file

---
## Platform

The embedded platform for this project is a Raspberry Pi Pico W connected to a Bosch BMA 530 over I2C. Additionally a P-channel mosfet is installed on the BMA530's power line to allow for hard resetting.

3 LEDs are also installed to allow for visual feedback

### Pico W GPIO Mapping:

#### I2C (to BMA530):

- GPIO 20: SDA
- GPIO 21: SCL

#### BMA530 Interrupt Pins:

- GPIO 19: INT1
- GPIO 18: INT2

#### BMA530 Power Control:

- GPIO 17: Mosfet Gate 

#### LED:

- GPIO 8: Red LED
- GPIO 9: Green LED
- GPIO 10: Blue LED

---

## Rule-Based Fall Detection
#### (firmware/fall_detect_rule_based)

The rule-based detector identifies a fall purely using interrupts from the BMA530. The first interrupt detects freefall while the second detects impact. If an impact is detected within 2 seconds of the most recent freefall event, a fall is registered.

- **Freefall Interrupt condition**:
  - Magnitude < 0.5g
  - Duration ≥ 300ms

- **Impact Interrupt condition**:
  - Any axis > 2g
  - Duration ≥ 30ms

The Pico W's MCU (rp2040) first initializes the BMA530 and configures it with these interrupts. It then sleeps and periodically checks if a fall was detected every second.

When an interrupt is detected from the BMA530, it is handled by the IRQ handler of the Pico W:

The freefall interrupt IRQ handler will update a timestamp indicating the last time free fall was detected.

The impact interrupt IRQ handler checks if the last free fall timestamp is within 2 seconds of the current time, if so, the `fall_detected` flag is set. 

This `fall_detected` flag can then be handled by the main program loop when convenient. Once handled, the flag should be cleared by calling `clear_fall_detected_flag()`.

---

## Machine Learning Based Fall Detection
#### (firmware/fall_detect_ml_based)

The Machine Learning (ML) based fall detector identifies a possible fall event using a free fall interrupt, then classifies the event as a fall or not using a trained Random Forest model. 

- **Model**: Random Forest
- **Input**: Extracted features from 3 seconds of BMA530 FIFO buffer centered around free fall event
- **Output**: Binary classification (fall / non-fall)

The free fall interrupt is configured identically to the rule based fall detection system:

- **Freefall Interrupt condition**:
  - Magnitude < 0.5g
  - Duration ≥ 300ms

When a free fall interrupt from the BMA530 is detected by the Pico W's IRQ handler, a `free_fall_detected` flag is set and a global sleep semaphore is released. 

The firmware's main driving loop (in `main.cpp`) sleeps for a second then checks `fall_occurred()` which immediately returns false if the `free_fall_detected` flag is not set. However, for sleeping the main loop uses the function `sleep_ms_or_until_sem(<time_ms>)` rather than `sleep_ms`. Therefore, if free fall is detected while the main loop is sleeping, the sleep function will immediately return, and the fall event is handled right away.

Handling of fall events is all done in `fall_occurred()` which returns true or false depending on whether a fall occurred or not. If `free_fall_detected` is true, the function will grab the BMA530's FIFO buffer for 3 seconds (centered around the free fall event). This is why the main loop needs to exit sleep and handle the fall event immediately, so it does not miss this FIFO window.

Once the FIFO buffer is grabbed, statistical and temporal features are extracted from it. The Random Forest Model has been trained on these features to classify if a fall has occurred.

These features are then fed into the Random Forest model (found in `rf_model.h` and generated by `tools/rf_train_test.py`). This model will deliver a classification (true or false) on whether a fall occurred or not which is then returned by `fall_occurred()`.

In this implementation, if the Debug build type is used, then when a fall is detected,  the features are streamed to the configured TCP server. An implementation of server is found in `server.py`

---

## Data Collection
#### (firmware/fall_detect_data_transmitter)

To collect data to train the fall detect ML model, firmware very similar to that of the detection firmware is used. 

Upon a free fall interrupt from the BMA530, the FIFO Buffer for 3 seconds centered around the fall event is grabbed from the BMA530 and stored into the Pico W's RAM.

The buffer is then streamed over TCP to a server to be labeled and stored. The Wi-Fi Network and server configuration is stored in `common.h`

An implementation of a the TCP server is found in `server.py`. In this program each received FIFO buffer is labeled and stored into its own file. It takes the following command line arguments:
`Usage: python server.py <folder_path> <start_file_number> <label (1 or 0)>`
- **Folder Path**: directory where data files will be saved
- **Start file number**: Number that the first file received will be named. the file name is incremeneted by one with each received buffer
- **label (0 or 1)**: value to label the data as. 1 means fall, while 0 means nonfall.

**All data gathered in this project can be found in `data/raw`**

---

## Data Parsing
#### (tools/data_parser.py)
Batch parse raw collected data files into CSV files:

`usage: data_parser.py [-h] -i INPUT -o OUTPUT [-d]`

- **-i**: Input directory of raw collected data files
- **-o**: Output directory to save parsed CSV files to
- **-d (Optional)**: Decode; translate raw accelerometer axis values into units of (g), and report time in seconds rather than milliseconds.

---

## Data Graphing
#### (tools/data_grapher.py)
Batch process directory of data CSV files (generated by `data_parser.py`), into matplot graphs of time vs. acceleration (for all 3 axes).

`usage: data_grapher.py [-h] -i INPUT -o OUTPUT`

- **-i**: Input directory of CSV data files
- **-o**: Output directory to save graphs to

---

## Feature Extraction
#### (tools/feature_extract.py)
Split directory of data CSV files (generated by `data_parser.py`), into a training set and testing set based on input training set percentage.Then extract features from each set and store as `training_features.csv` and `testing_features.csv`. Additionally the data files for each set are copied into a `train_files` and `test_files` directory for record keeping.

`usage: feature_extract.py [-h] -i INPUT -o OUTPUT [-p PERCENT]`

- **-i**: Input directory of CSV data files
- **-o**: Output directory to store feature csv files and copied data file directories
- **-p (Optional)**: Percentage of input data randomly sampled to be training data (Default = 80).

---

## ML (Random Forest) Training & Testing
#### (tools/rf_train_test.py)

Train Random Forest binary classifier model using `training_features.csv` and test using `testing_features.csv`; both of these files are generated by `feature_extract.py`. 

This script will output `rf_model.h` to use in the ML based fall detect firmware, as well as `confusion_matrix_ml.png` and `feature_importance.png`.

`usage: rf_train_test.py [-h] -tr TRAIN -te TEST [-n N_ESTIMATORS] [-s SEED]`

- **-tr**: `training_features.csv` file path
- **-te**: `testing_features.csv` file path
- **-n (Optional)**: Number of trees used in Random Forest model (Default = 100).
- **-s (Optional)**: Seed used in Random Forest model training (Default = 42)


#### (tools/rf_train_n_estimators_accuracy.py)

Train Random Forest binary classifier model using `training_features.csv` and test using `testing_features.csv` for each value of n in input range `N_ESTIMATORS_RANGE`. 

Once complete, the graph `accuracy_vs_trees.png` is generated to describe the accuracy of the model for each value of n.

`usage: rf_train_n_estimators_accuracy.py [-h] -tr TRAIN -te TEST [-n N_ESTIMATORS_RANGE] [-s SEED]`
`usage: 
- **-tr**: `training_features.csv` file path
- **-te**: `testing_features.csv` file path
- **-n (Optional)**: Range of trees used in Random Forest model (Default = range(1, 200, 1)).
- **-s (Optional)**: Seed used in Random Forest model training (Default = 42)

## Rule-Based Fall Detect Evaluator
#### (tools/rule_based_validator.py)

Simulate the classification of the rule based fall detection system on a batch of testing data CSV files.

Accuaracy measures and a confusion matrix are generated (`confusion_matrix_rule_based.png`) to allow for comparision with the ML based system (if same testing data files are used).

`usage: rule_based_validator.py [-h] -i INPUT`

- **-i**: Input directory of CSV data files


## Real Fall Vs. Simulated Fall Comparison
#### (tools/real_vs_sim_compare.py)

Generate comparison statistics and graphs which compare the features of one directory of fall data CSVs with another. The intended use is to compare a directory of real fall data CSVs with simulated ones to provide credence to the simulation.

`usage: real_vs_sim_compare.py [-h] -r REAL -s SIM`


- **-r**: Input directory of CSV data files for real fall events
- **-s**: Input directory of CSV data files for simulated fall events