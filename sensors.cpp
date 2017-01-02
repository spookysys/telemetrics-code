#include "sensors.hpp"
#include "pins.hpp"
#include "events.hpp"
#include <Wire.h>
#include <array>
#include "wiring_private.h" // pinPeripheral() function
namespace 
{
    Stream& logger = Serial;
        
    // I2C scan function
    void I2CScan()
    {
      // scan for i2c devices
      byte error, address;
      int nDevices;
    
      logger.println("Scanning...");
    
      nDevices = 0;
      for (address = 1; address < 127; address++ )
      {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
    
        if (error == 0)
        {
          logger.print("I2C device found at address 0x");
          if (address < 16)
            logger.print("0");
          logger.print(address, HEX);
          logger.println("  !");
    
          nDevices++;
        }
        else if (error == 4)
        {
          logger.print("Unknow error at address 0x");
          if (address < 16)
            logger.print("0");
          logger.println(address, HEX);
        }
      }
      if (nDevices == 0)
        logger.println("No I2C devices found\n");
      else
        logger.println("done\n");
    
    }
    

    void writeByte(uint8_t address, uint8_t subAddress, uint8_t data)
    {
        Wire.beginTransmission(address);  // Initialize the Tx buffer
        Wire.write(subAddress);           // Put slave register address in Tx buffer
        Wire.write(data);                 // Put data in Tx buffer
        Wire.endTransmission();           // Send the Tx buffer
    }
    
    uint8_t readByte(uint8_t address, uint8_t subAddress)
    {
        uint8_t data; // `data` will store the register data
        Wire.beginTransmission(address);         // Initialize the Tx buffer
        Wire.write(subAddress);                  // Put slave register address in Tx buffer
        Wire.endTransmission(false);             // Send the Tx buffer, but send a restart to keep connection alive
        Wire.requestFrom(address, (size_t) 1);   // Read one byte from slave register address
        data = Wire.read();                      // Fill Rx buffer with result
        return data;                             // Return data read from slave register
    }
    
    void readBytes(uint8_t address, uint8_t subAddress, uint8_t count, uint8_t * dest)
    {
        Wire.beginTransmission(address);   // Initialize the Tx buffer
        Wire.write(subAddress);            // Put slave register address in Tx buffer
        Wire.endTransmission(false);       // Send the Tx buffer, but send a restart to keep connection alive
        uint8_t i = 0;
        Wire.requestFrom(address, (size_t) count);  // Read bytes from slave register address
        while (Wire.available()) {         // Put read results in the Rx buffer
            dest[i++] = Wire.read();
        }
    }
    

    // accelerometer and gyroscope
    class Imu
    {
        static const uint8_t ADDR = (!pins::MPU_ADO) ? 0x68 : 0x69;
        static const uint8_t WHO_AM_I = 0x75;
        static const uint8_t WHO_AM_I_ANSWER = 0x71;


        static const uint8_t SMPLRT_DIV = 0x19;
        static const uint8_t CONFIG = 0x1A;
        static const uint8_t GYRO_CONFIG = 0x1B;
        static const uint8_t ACCEL_CONFIG = 0x1C;
        static const uint8_t ACCEL_CONFIG2 = 0x1D;

        static const uint8_t FIFO_EN = 0x23;
        static const uint8_t I2C_MST_CTRL = 0x24;
        
        static const uint8_t INT_PIN_CFG = 0x37;
        static const uint8_t INT_ENABLE = 0x38;

        static const uint8_t INT_STATUS = 0x3A;

        static const uint8_t USER_CTRL = 0x6A;
        static const uint8_t PWR_MGMT_1 = 0x6B; 
        static const uint8_t PWR_MGMT_2 = 0x6C;

        static const uint8_t FIFO_COUNTH = 0x72;
        static const uint8_t FIFO_COUNTL = 0x73;
        static const uint8_t FIFO_R_W = 0x74;

    private:
        int gyro_scale = 0; // 250 dps
        int accel_scale = 0; // 2 g
        
    public:
        // Howto interrupt: https://github.com/kriswiner/MPU-9250/issues/57
        
        bool setup()
        {
            bool ok = true;
            
            // Identify
            uint8_t c = readByte(ADDR, WHO_AM_I);
            if (c != WHO_AM_I_ANSWER) {
                logger.println(String("MPU failed to identify: ") + String(c, HEX));
                ok = false;
            }
            
            // Reset
            writeByte(ADDR, PWR_MGMT_1, 0x80);
            delay(100);

            // Setup clock
            writeByte(ADDR, PWR_MGMT_1, 0x01);
            delay(200);
            
            // SMPLRT_DIV
            // Data output (fifo) sample rate
            // Set to 0 for the maximum 1 kHz (= internal sample rate)
            //writeByte(ADDR, SMPLRT_DIV, 0x04); // 200 Hz
            writeByte(ADDR, SMPLRT_DIV, 0x20); // sloooow

            // CONFIG
            // [6] Fifo behaviour on overflow - 0:drop oldest data, 1:drop new data
            // [5:3] fsync mode - 0: disabled
            // [2:0] DLPF_CFG - Gyro/Temperature filter bandwidth - 0:250Hz, 1:184Hz, 2:92Hz, 3:41Hz..., 5:5Hz and 7:3600Hz
            writeByte(ADDR, CONFIG, 0x43); // 41Hz, 5.9ms delay
    
            // GYRO_CONFIG
            // [7:5] Gyro Self-Test [XYZ] - 0:disabled
            // [4:3] Gyro Scale - 0: 250dps, 1:500dps, 2:1000dps, 3:2000dps
            // [1:0] Fchoice_b - Gyro/Temperature filter enable - 0:enabled
            assert(!(gyro_scale%4));
            writeByte(ADDR, GYRO_CONFIG, gyro_scale<<3);
            
            // ACCEL_CONFIG2
            // [3] fchoice_b - Filter enable - 0:enabled
            // [2:0] DLPF_CFG - Filter bandwidth - (complicated)
            writeByte(ADDR, ACCEL_CONFIG2, 0x03); // 41Hz

            // ACCEL_CONFIG
            // [2:0] Accelerometer Self-Test [XYZ] - 0:disabled
            // [4:3] Accelerometer Scale - 0:2g, 1:4g, 2:8g, 3:16g
            assert(!(accel_scale%4));
            writeByte(ADDR, ACCEL_CONFIG, accel_scale<<3);

            // Interrupt pin config
            // [7] int pin is active 0:high, 1:low
            // [6] int pin is 0:push-pull 1:open-drain
            // [5] int pin is held until 0:50us 1:status is cleared
            // [4] int status is cleared by 0:reading int status register 1:any read operation
            // [3] fsync as an interrupt is active 0:high 1:low
            // [2] fsync as an interrupt is 0:disabled 1:enabled
            // [1] bypass_en, affects i2c master pins
            //writeByte(ADDR, INT_PIN_CFG, 0x22); // INT is high until status register is read
            //writeByte(ADDR, INT_PIN_CFG, 0x12);  // INT is 50 microsecond pulse and any read to clear
            writeByte(ADDR, INT_PIN_CFG, 0x02); // INT is 50ms pulse or until status register is read
        
            // Reset fifo and signal paths
            writeByte(ADDR, USER_CTRL, 0x05);
            delay(25);  // Delay a while to let the device stabilize
            
            // USER_CTRL
            // Bit 7 enable DMP, bit 3 reset DMP (secret)
            // [6] FIFO_EN 1:enable
            // [2] FIFO_RST 1:reset
            // [0] SIG_COND_RST: 1:reset signal paths and sensor registers
            writeByte(ADDR, USER_CTRL, 0x40); // enable fifo

            // FIFO_EN
            // [6:4] gyro xyz
            // [3] accel
            writeByte(ADDR, FIFO_EN, 0x78); // gyro and accel

            // Enable Raw Sensor Data Ready interrupt to propagate to interrupt pin
            writeByte(ADDR, INT_ENABLE, 0x01);  // Enable data ready (bit 0) interrupt

            return ok;
        }
        
        void update()
        {
            // Full gyro and accelerometer data
            static const int packet_size = 12;
            
            // Number of bytes in fifo
            uint16_t fifo_bytes;
            readBytes(ADDR, FIFO_COUNTH, 2, (uint8_t*)&fifo_bytes);
            fifo_bytes = misc::swapEndianness(fifo_bytes);
            uint16_t packet_count = fifo_bytes / packet_size;
            
            // Read from fifo
            if (packet_count) {
                std::array<int16_t, packet_size/2> packet_data;
                for (int i=0; i<packet_count; i++) {
                    readBytes(ADDR, FIFO_R_W, packet_size, (uint8_t*)&packet_data[0]);
                }
                /*
                logger.print(String("Packets: ") + String(packet_count) + "\t");
                for (int i=0; i<packet_size/2; i++) logger.print(String(misc::swapEndianness(packet_data[i])) + "\t");
                logger.println();
                */
            }
            
        }
        
        uint8_t readInterruptStatus() {
            return readByte(ADDR, INT_STATUS);
        }
    };

    class Magnetometer
    {
        static const uint8_t ADDR = 0x0C;
        
        static const uint8_t WHO_AM_I = 0x00;
        static const uint8_t WHO_AM_I_ANSWER = 0x48;

        static const uint8_t INFO = 0x01;
        static const uint8_t ST1 = 0x02;  // data ready status bit 0
        static const uint8_t XOUT_L = 0x03;  // data
        static const uint8_t XOUT_H = 0x04;
        static const uint8_t YOUT_L = 0x05;
        static const uint8_t YOUT_H = 0x06;
        static const uint8_t ZOUT_L = 0x07;
        static const uint8_t ZOUT_H = 0x08;
        static const uint8_t ST2 = 0x09;  // Data overflow bit 3 and data read error status bit 2
        static const uint8_t CNTL = 0x0A;  // Power down (0000), single-measurement (0001), self-test (1000) and Fuse ROM (1111) modes on bits 3:0
        static const uint8_t ASTC = 0x0C;  // Self test control
        static const uint8_t I2CDIS = 0x0F;  // I2C disable
        static const uint8_t ASAX = 0x10;  // Fuse ROM x-axis sensitivity adjustment value
        static const uint8_t ASAY = 0x11;  // Fuse ROM y-axis sensitivity adjustment value
        static const uint8_t ASAZ = 0x12;  // Fuse ROM z-axis sensitivity adjustment value

    private:
        std::array<float, 3> destination;
    public:
        bool setup()
        {
            bool ok = true;
            
            uint8_t c = readByte(ADDR, WHO_AM_I);
            if (c != WHO_AM_I_ANSWER) {
                logger.println(String("Magnetometer failed to identify: ") + String(c, HEX));
                ok = false;
            }
            
            // First extract the factory calibration for each magnetometer axis
            writeByte(ADDR, CNTL, 0x00); // Power down magnetometer  
            delay(10);
            writeByte(ADDR, CNTL, 0x0F); // Enter Fuse ROM access mode
            delay(10);
            uint8_t rawData[3];  // x/y/z gyro calibration data stored here
            readBytes(ADDR, ASAX, 3, &rawData[0]);  // Read the x-, y-, and z-axis calibration values
            destination[0] =  (float)(rawData[0] - 128)/256. + 1.;   // Return x-axis sensitivity adjustment values, etc.
            destination[1] =  (float)(rawData[1] - 128)/256. + 1.;  
            destination[2] =  (float)(rawData[2] - 128)/256. + 1.; 
            writeByte(ADDR, CNTL, 0x00); // Power down magnetometer  
            delay(10);
            // Configure the magnetometer for continuous read and highest resolution
            // set Mscale bit 4 to 1 (0) to enable 16 (14) bit resolution in CNTL register,
            // and enable continuous mode data acquisition Mmode (bits [3:0]), 0010 for 8 Hz and 0110 for 100 Hz sample rates
            writeByte(ADDR, CNTL, 0x16); // 16 bit, 100Hz acquisition
            delay(10);
            
            return ok;
        }
    };

    class Altimeter
    {
        static const uint8_t ADDR = 0x76;
        static const uint8_t WHO_AM_I_ADDR = 0xD0;
        static const uint8_t WHO_AM_I_ANSWER = 0x58;
    public:
        bool setup()
        {
            bool ok = true;
            
            uint8_t c = readByte(ADDR, WHO_AM_I_ADDR);
            if (c != WHO_AM_I_ANSWER) {
                logger.println(String("Altimeter failed to identify: ") + String(c, HEX));
                ok = false;
            }
            
            return ok;
        }
    };
    
    
    

    Imu imu;
    Magnetometer magnetometer;
    Altimeter altimeter;

    void imuIsr() {
        imu.update();
        logger.write('.');
        imu.readInterruptStatus();
    }

/*
    auto& imu_proc = events::makeProcess("imu").setPeriod(10).subscribe([&](unsigned long time, unsigned long delta) {
        imu.update();
        //logger.println(digitalRead(pins::MPU_INT));
        //imu.readInterruptStatus();
        //isr();
    });
 */
    
} // anon


namespace sensors
{

    bool setup()
    {
        I2CScan();

        // setup all my sensors
        bool imu_ok = imu.setup();
        bool mag_ok = magnetometer.setup();
        bool alt_ok = altimeter.setup();
        assert(imu_ok);
        assert(mag_ok);
        assert(alt_ok);

        // I use the MPU interrupt to drive realtime update for control
        pinMode(pins::MPU_INT, INPUT);
        attachInterrupt(pins::MPU_INT, imuIsr, RISING);
                
        return imu_ok && mag_ok && alt_ok;
    }
    
}