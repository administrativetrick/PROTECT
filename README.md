# PROTECT
**P**redatory **R**ecognition and **O**bservation **T**hrough **E**nhanced **C**onversational **T**racking

## Uses
This is meant to be a suite of tools to assist anyone investigating matters of Child Sexual Abuse that will grow over time. This suite focuses on providing tools for the monitoring of chat logs, and interactions that make having predatory conversations with minors more difficult.

### The tools are as follows

#### ChatLogsParser
It can be used by parents, law enforcement, and other concerned parties to parse chat logs for a list of keywords
There is a preference JSON file, you can add a list of JSON keywords into that file and the program will parse for those keywords.
You can also add the comma separated list into the preferences box in the application and it will generate the JSON file for you.
Preferences Persist across reboots, and chat logs are stored in a folder called chats.

#### ImageMetadataScraper
This can be used to scrape a page for images. It does not download the images, it only scrapes the page for metadata and stores that metadata to a database for later investigation.

#### websiteCloner
This can be used to pull and Dave the HTML from websites. images are intentionally not saved as a part of the clone.
