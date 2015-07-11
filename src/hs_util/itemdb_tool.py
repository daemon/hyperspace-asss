#!/usr/bin/python

import io
import json
import mysql.connector
import os.path
import sys

from mysql.connector import errorcode
from optparse import OptionParser



def get_item_categories(dbc, item_id):
    cursor = dbc.cursor()

    query = """
        SELECT c.name
        FROM hs_categories c
        INNER JOIN hs_category_items ci ON ci.category_id = c.id
        WHERE ci.item_id = %s
    """

    cursor.execute(query, (item_id,))

    categories = []
    for (category,) in cursor:
        categories.append(category)

    cursor.close()

    return categories

def get_item_types(dbc, item_id):
    cursor = dbc.cursor()

    query = """
        SELECT t.name, it.qty
        FROM hs_item_types t
        INNER JOIN hs_item_type_assoc it ON it.type_id = t.id
        WHERE it.item_id = %s
    """

    cursor.execute(query, (item_id,))

    types = {}
    for (type, quantity) in cursor:
        types[type] = quantity

    cursor.close()

    return types

def get_item_properties(dbc, item_id):
    cursor = dbc.cursor()

    query = """
        SELECT name, value, absolute, ignore_count
        FROM hs_item_properties WHERE item_id = %s
    """

    cursor.execute(query, (item_id,))

    properties = {}
    for (name, value, absolute, ignore_count) in cursor:
        if ignore_count:
            value = "!" + str(value)

        if absolute:
            value = "=" + str(value)

        properties[name] = value

    cursor.close()

    return properties

def get_item_events(dbc, item_id):
    cursor = dbc.cursor()

    query = """
        SELECT event, action, data, message
        FROM hs_item_events WHERE item_id = %s
    """

    cursor.execute(query, (item_id,))

    events = []
    for (name, action, data, message) in cursor:
        events.append({
            "event": name,
            "action": action,
            "data": data,
            "message": message
        })

    cursor.close()

    return events

def get_item_stores(dbc, item_id):
    cursor = dbc.cursor()

    query = """
        SELECT s.name
        FROM hs_stores s
        INNER JOIN hs_store_items si ON si.store_id = s.id
        WHERE si.item_id = %s
    """

    cursor.execute(query, (item_id,))

    stores = []
    for (name,) in cursor:
        stores.append(name)

    cursor.close()

    return stores


def get_items(dbc, arena_id):
    cursor = dbc.cursor()

    query = """
        SELECT i.id, i.name, i.short_description, i.long_description, i.buy_price, i.sell_price, i.exp_required, i.ships_allowed, i.max
        FROM hs_items i
        INNER JOIN hs_category_items ci ON i.id = ci.item_id
        INNER JOIN hs_categories c ON ci.category_id = c.id
        WHERE c.arena = %s
    """

    cursor.execute(query, (arena_id,))

    items = []

    for (id, name, short_description, long_description, buy_price, sell_price, exp_required, ships_allowed, max) in cursor:
        item = {
            "id": id,
            "name": name,
            "short_desc": short_description,
            "long_desc": long_description,
            "buy_price": buy_price,
            "sell_price": sell_price,
            "exp_required": exp_required,
            "ships_allowed": [],
            "max": max,
        }

        # convert bit field to friendly numbers for allowed ship types
        for ship in range(0, 8):
            if ships_allowed & (1 << ship):
                item["ships_allowed"].append(ship + 1)

        items.append(item)

    cursor.close()
    return items


def export_items(dbc, file, arena_id):

    with io.open(file, 'wb') as outfile:
        item_data = {
            "items": get_items(dbc, arena_id)
        }

        for item in item_data["items"]:
            # get item categories
            item["categories"] = get_item_categories(dbc, item["id"])

            # get item types
            item["types"] = get_item_types(dbc, item["id"])

            # get item properties
            item["properties"] = get_item_properties(dbc, item["id"])

            # get item events
            item["events"] = get_item_events(dbc, item["id"])

            # get item stores
            item["stores"] = get_item_stores(dbc, item["id"])

            # clear ID for output
            item.pop("id")

        json.dump(item_data, outfile, indent=2)


def import_items(dbc, file, arena_id):
# For each item:
#   - Validate item types
#   - Validate stores

#   - Find existing item with same name
#     - If exists, rename to item name (l#) where # is the next available legacy number for that item
#   - Write new item to database, capturing the item ID
#   - Insert new item properties
#   - Insert new item events
#   - Add item type references
#   - Add item to stores


    pass





def get_db_connection(host, database, username, password):
    try:
        dbc = mysql.connector.connect(host=host, database=database, user=username, password=password)
        return dbc
    except mysql.connector.Error as e:
        if e.errno == errorcode.ER_ACCESS_DENIED_ERROR:
            print("Invalid username and/or password")
        elif e.errno == errorcode.ER_BAD_DB_ERROR:
            print("Database %s does not exist", database)
        else:
            print(e)

    return None


def main(argv):
    parser = OptionParser()
    parser.add_option("-e", "--export", action="store_const", const="export", dest="action")
    parser.add_option("-i", "--import", action="store_const", const="import", dest="action")
    parser.add_option("-a", "--arena", action="store", dest="arena_id", default="main")
    parser.add_option("--dbhost", action="store", default="localhost")
    parser.add_option("--database", action="store", default="hyperspace")
    parser.add_option("--dbuser", action="store", default="hyperspace")
    parser.add_option("--dbpass", action="store", default=None)
    (options, args) = parser.parse_args()

    file = "items.json"
    for item in args:
        file = item
        break

    # Setup DB connection
    dbc = get_db_connection(options.dbhost, options.database, options.dbuser, options.dbpass)

    if dbc is not None:
        if (options.action == "export"):
            export_items(dbc, file, options.arena_id)

        elif (options.action == "import"):
            import_items(dbc, file, options.arena_id)

        else:
            print("ERROR: Must specify an action to perform")

        dbc.close()

main(sys.argv)
