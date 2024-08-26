This is a simple C program that will analyze your log files.

Works by reading lines with WARN, CRITICAL, ERROR.

You can analyze one or multiple log files at once.

## Example

``` ./log_analyzer example.log ```

Will analyze your log file.

```./log_analyzer logs/ ```

Will analyze your all log files in the specified directory.


You can also real-time monitor your log files.

## Example

``` ./log_analyzer --monitor example.log ```

for one file. or

``` ./log_analyzer --monitor logs/ ```

for entire directory

## Install

Simply compile it with gcc.
