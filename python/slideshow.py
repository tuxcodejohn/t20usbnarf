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
class DirChoice:

    def __init__(self, paths):
        self.random = random.Random()
        self.files = files = []
        
        for path in paths:
            if os.path.isfile(path):
                files.append(path)
            elif os.path.isdir(path):
                files.extend(read_list(path))
            else:
                print "File '%s' not found!" % path

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

        print "fetching ..."

        for url in urls:
            feed = feedparser.parse(url)

            for entry in feed.entries:
                if 'enclosures' in entry:
                    for enclosure in entry.enclosures:
                        if enclosure.type.startswith('image'):
                            hrefs.append(enclosure.href)

        self.last_fetch = time.time()

    def choice(self):
        if self.last_fetch + FETCH_INTERVAL < time.time():
            self.fetch()

        url = self.random.choice(self.hrefs)
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

