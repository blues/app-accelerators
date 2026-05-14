function formatTime(timeVal) {
  var date = new Date(timeVal * 1000);
  var hours = date.getHours();
  var minutes = "0" + date.getMinutes();
  var seconds = "0" + date.getSeconds();

  return hours + ':' + minutes.substr(-2) + ':' + seconds.substr(-2);
}

function timeSinceMovement(current, lastMovement) {
  current = parseInt(current);
  last = parseInt(lastMovement);

  return Math.ceil((current - last) / 60);
}

function Decoder(request) {
  var data = JSON.parse(request.body);
  var device = data.device;

  var deviceKey = Object.keys(measurements)[0];
  var measurementsValues = measurements[deviceKey];

  var file = data.file;
  var lastUpdate = data.when;

  var decoded = {};

  if (file === "_motion.qo") {
      decoded.voltage = data.body.voltage;
      decoded.motion = data.body.motion;
      decoded.movements = data.body.movements;
      decoded.orientation = data.body.orientation;
      decoded.voltage = data.body.voltage;
      decoded.last_movement = data.when;
      decoded.formatted_last_movement = formatTime(data.when);
      if (decoded.motion) {
          decoded.strokes_per_minute = Math.ceil(decoded.motion / 2);
      } else {
          decoded.strokes_per_minute = 0;
      }
  } else if (file === "_log.qo") {
      decoded.message = data.body.text;
  } else if (file === "_session.qo") {
      decoded.message = data.body.why;
  }

  if (("tower_lat" in data) && ("tower_lon" in data)) {
      decoded.tower_location = "(" + data.tower_lat + "," + data.tower_lon + ")";
  }
  if (("where_lat" in data) && ("where_lon" in data)) {
      decoded.device_location = "(" + data.where_lat + "," + data.where_lon + ")";
  }

  decoded.rssi = data.rssi;
  decoded.bars = data.bars;
  decoded.card_temperature = data.body.temperature;
  decoded.last_update = data.when;
  decoded.formatted_last_update = formatTime(data.when);

  decoded.time_since_movement = timeSinceMovement(lastUpdate, measurementsValues.LAST_MOVEMENT.value);

  if (decoded.time_since_movement >= 15) {
      decoded.alert = "Pump Jack Has Stopped Moving!";
  } else {
      decoded.alert = "Pump Jack Operational";
  }

  // Array where we store the fields that are being sent to Datacake
  var datacakeFields = []

  // take each field from decodedElsysFields and convert them to Datacake format
  for (var key in decoded) {
      if (decoded.hasOwnProperty(key)) {
          datacakeFields.push({field: key.toUpperCase(), value: decoded[key], device: device})
      }
  }

  // forward data to Datacake
  return datacakeFields;
}
