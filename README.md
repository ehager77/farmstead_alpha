# farmstead

farmstead is group of open-source data-loggers built on the NODEMCU 12-E meant for farmers, researchers and other plant enthusiasts.

[photo1]:https://photos.google.com/u/1/photo/AF1QipNnMWe11bNcos-sPmFwRhbuZ1OGqHwSzXPjWClV
[photo2]: https://photos.google.com/u/1/photo/AF1QipPAgEQ8Qwf1x9ccwVBVayWgW5sxRKN5PWLV-oqW
[photo3]: https://photos.google.com/u/1/photo/AF1QipOnWj2yRV5rtKphlWHvTlV7uzlXjBBBXWx9nXSs

## Bill of Materials

Follow this [link.](https://docs.google.com/spreadsheets/d/12NdWIi1DjhQLWsaMIDiaYoft81Pou43TMq7iLpbZecI/edit?usp=sharing)

## Installation & Requirements

Simply clone the repository and open the files.  You will need to have Arduino IDE 1.8+ installed to open them. Before flashing, please make sure you have selected a NODEMCU 12-E from Tools --> Board --> Board Manager in the Arduino IDE.

You will also need a Ubidots account and your own account token.  It's free to use so don't worry.

Simply replace the following line to add send data to your Ubidots dashboard:

```c++
const char* UBIDOTS_TOKEN = "YOUR_TOKEN";
``` 

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

Please make sure to update tests as appropriate.

## License
[MIT](https://choosealicense.com/licenses/mit/)