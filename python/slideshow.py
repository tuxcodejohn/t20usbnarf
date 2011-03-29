import sys
import random
import time
import os.path
import urllib
import StringIO

import feedparser

import Image, ImageDraw

from t20 import ImageBeamer

FETCH_INTERVAL = 15 * 60
WAIT_INTERVAL = 15

file_extensions = ['.jpg', '.jpeg', '.png', '.gif']

def read_list(path):
    for rel in sorted(os.listdir(path), key=str.lower):
        abs_path = os.path.join(path, rel)
        if os.path.isdir(abs_path):
            for sub in read_list(abs_path):
                yield sub
        elif os.path.splitext(rel)[1].lower() in file_extensions:
            yield abs_path

class FileChoice:

    def __init__(self, paths):
        self.files = files = []
        
        for path in paths:
            if os.path.isfile(path):
                files.append(path)
            elif os.path.isdir(path):
                files.extend(read_list(path))
            else:
                print "File '%s' not found!" % path

        random.shuffle(files)

    def choice(self):
        files= self.files

        path = files[0]

        del files[0]
        files.append(path)

        return path

class FeedChoice:

    def __init__(self, urls):
        self.urls = urls
        self.hrefs = []
        self.random = random.Random()
        self.last_fetch = 0

        self.fetch()

    def fetch(self):
        self.hrefs = hrefs = []
        urls = self.urls
        random = self.random

        new_hrefs = []

        print "fetching ..."

        for url in urls:
            feed = feedparser.parse(url)

            for entry in feed.entries:
                if 'enclosures' in entry:
                    for enclosure in entry.enclosures:
                        if enclosure.type.startswith('image'):
                            new_hrefs.append(enclosure.href)

        del_list = []
        for index, href in enumerate(hrefs):
            if href not in new_hrefs:
                del_list.append(index)

        for index in reversed(del_list):
            del hrefs[index]

        for href in new_hrefs:
            index = random.randint(0, len(hrefs))
            hrefs.insert(index, href)

        self.last_fetch = time.time()

    def choice(self):
        if self.last_fetch + FETCH_INTERVAL < time.time():
            self.fetch()

        hrefs = self.hrefs

        url = hrefs[0]

        del hrefs[0]
        hrefs.append(url)

        print "loading image .."
        data = urllib.urlopen(url).read()
        return StringIO.StringIO(data)

# finding the beamer
beamer = ImageBeamer()

if len(sys.argv) < 2:
    print "Please pass at least one destination"
    sys.exit(1)

# the first argument determines the type of all arguments!
first = sys.argv[1]
args = sys.argv[1:]
if first.startswith("http://"):
    print "Interpreting everything as feed!"
    choice = FeedChoice(args)
else:
    print "Interpreting everything as files!"
    choice = FileChoice(args)

# initializing the beamer
target_res = beamer.resolution
beamer.send_init()

while True:
    try:
        orig = Image.open(choice.choice())

        scale = min(float(target) / old for old, target in zip(orig.size, target_res))
        new_size = tuple(int(old * scale) for old in orig.size)

        scaled = orig.resize(new_size, Image.ANTIALIAS)

        top = tuple((target - new) / 2 for new, target in zip(new_size, target_res))

        im = Image.new(mode="RGB", size=target_res)
        im.paste(scaled, top)

        print "sending new image ..."
        beamer.send_image(im)
        print "new image uploaded"

        time.sleep(WAIT_INTERVAL)
    except Exception as e:
        print e

