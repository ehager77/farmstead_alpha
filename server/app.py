from flask import Flask, request
import paho.mqtt.client as mqtt
from flask_sqlalchemy import SQLAlchemy
from flask_migrate import Migrate, MigrateCommand
from flask_script import Manager
import psycopg2
import time

app = Flask(__name__)

app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False 
app.config['SQLALCHEMY_DATABASE_URI'] = 'postgres://tsdbadmin:i2hsh1cqo1awejcz@tsdb-3b2f6584-isabel-9aa5.a.timescaledb.io:10649/test?sslmode=require'
db = SQLAlchemy(app)
migrate = Migrate(app, db)
manager = Manager(app)
manager.add_command('db', MigrateCommand)

devices = []

class Device(object):
    def __init__(self, id, deviceType, localIP, location, latitude, longitude):
        self.id = id
        self.deviceType = deviceType
        self.localIP = localIP
        self.location = location
        self.latitude = latitude
        self.longitude = longitude

# def add_devices(client, userdata, messages):
#     if message.topic == "device"


# Get log information on connection status
def on_log(client, userdata, level, buf):
    print("Log: " + buf)

# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected with result code "+str(rc))
    else:
        print("Bad connection.  Returned result code: ", rc)

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    python_client.subscribe("device/")

# The callback for when a PUBLISH message is received from the ESP8266.
def on_message(client, userdata, message):
    if message.topic == "device/":
        print("Device found...")
        print(message.payload.decode("utf-8"))
        deviceId = message.payload.decode("utf-8")
        if deviceId not in devices:
            devices.append(deviceId)
        print("Devices subscribed:")
        print(devices)
        for deviceId in devices:
            deviceTopicStr = "device/" + deviceId + "/#"
            python_client.subscribe(deviceTopicStr) 
        #print(dhtreadings_json['temperature'])
        #print(dhtreadings_json['humidity']

broker = "35.236.34.162"
port = 1883
keepAlive = 120      
python_client = mqtt.Client()

python_client.on_connect = on_connect
python_client.on_log = on_log
print("Connecting to broker " + broker)

python_client.connect(broker,port,keepAlive)
python_client.loop_start()

python_client.on_message = on_message

class User(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    email = db.Column(db.String(120), unique=True)
    phoneNumber = db.Column(db.String(20), unique=True)
    password= db.Column(db.String(60))
    lastName= db.Column(db.String(30))
    firstName= db.Column(db.String(30))
    role = db.Column(db.String(30))

    def __init_(self, username, email, phoneNumber, password, lastName, firstName, role):
        self.username = username
        self.email = email
        self.phoneNumber = phoneNumber
        self.password = password
        self.lastName = lastName
        self.firstName = firstName
        self.role = role

    def __repr__(self):
        return '<User %r>' % self.username

@app.route("/")
def hello():
    return "<h1 style= 'color:red'>"+deviceId+"</h1>"

if __name__ == '__main__':
    manager.run()
    app.run()
