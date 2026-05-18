import argparse
import can

parser = argparse.ArgumentParser(
    description="Send a single CAN packet on a CAN bus.")
parser.add_argument("--dev", help=("The interface name of the USB to CAN "
    "converter (default: can0)."), default="can0")
parser.add_argument("--id", help=("The ID to put in the packet, specified in "
    "hex. Example: ab12."),
    required=True)
parser.add_argument("--data",
    help=("The data to put in the packet. Specified in hex, with colons"
          " separating bytes, up to a max of 8 bytes. Example: "
          "01:02:03:0a:ff:dd:ee:56."), required=True)
args = vars(parser.parse_args())

bus = can.interface.Bus(channel=args["dev"], bustype="socketcan")
pkt_id = int(args["id"], 16)
data = [int(num, 16) for num in args["data"].split(":")]
pkt = can.Message(arbitration_id=pkt_id, data=data)
bus.send(pkt)
