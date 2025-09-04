This project was created to incentivize my special needs sister to take more walks. The physical/Arduino part works by uploading map points when she leaves and returns to the house. The points are uploaded to a fastapi server which generates reports which are automatically printed that include a map and her favorite photos.

## Contents
- Physical: This folder contains the 3d models for the enclosures and the part list to create the hardware
- Arduino: The code that can be flashed to the esp32
- Server: Datapoints are uploaded to here from the device, generates the reports and does associated actions


While I doubt a getting started is needed as this project is probably too unique to be of much use to other people, the server can be started with `docker compose up -d` after adding the required environment vars in the compose.yml file. These are all in `./Server`
