#!/usr/bin/python

import sys
from optparse import OptionParser



def validate_item(item):


def export_items(file):

def import_items(file);





def main(argv):
    parser = OptionParser()
    parser.add_option("-e", "--export", action="store_const", const="export", dest="action")
    parser.add_option("-i", "--import", action="store_const", const="import", dest="action")
    (options, args) = parser.parse_args()

    print("Options: %s\nArgs:%s" % (options, args))


    if (options.action == "export"):
        # do export
        print("DO EXPORT")
        pass
    else if (options.action == "import"):
        # do import
        print("DO IMPORT")
        pass
    else
        print("ERROR: Must specify an action to perform")


