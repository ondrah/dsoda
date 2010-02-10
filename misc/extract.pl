#!/usr/bin/perl
#       _ _       _ _        _                 _       
#    __| (_) __ _(_) |_ __ _| |  ___  ___   __| | __ _ 
#   / _` | |/ _` | | __/ _` | | / __|/ _ \ / _` |/ _` |
#  | (_| | | (_| | | || (_| | | \__ \ (_) | (_| | (_| |
#   \__,_|_|\__, |_|\__\__,_|_| |___/\___/ \__,_|\__,_|
#           |___/                written by Ondra Havel
#
# EZ-USB firmware extractor utility.
# Locates the firmware in supplied Windows driver file using objdump
# from mingw32-binutils. The result is converted and saved in ihex
# format for later use with the fxload utility.
# This utility also generates suitable udev rules file which
# can be used for instant firmware loading.
# All newly generated files are stored in current working directory.

use strict;
use warnings;
use Getopt::Long;

my $objdump='i586-mingw32msvc-objdump';
my $model='dso2250';
my $firmware_dir='/lib/firmware';
my $product_id='2250';

sub usage {
	print<<"E";
usage: $0 [--objdump=$objdump] [--help] 
		   [--model=$model] [--firmware_dir=$firmware_dir] [--product-id=$product_id] driverfile.sys
  --objdump   set custom win32-aware objdump application
  --model  set dso model name, used for proper filenaming
           and the (udev) rules file
  --firmware_dir  specify system's firmware directory (used in the rules file)
  --product_id  specify custom product_id (used in the rules file)

Current default values are shown on the usage: line.
E
	exit 0;
}

my @symbols=('_loader','_firmware');

GetOptions("objdump=s"=>\$objdump,"help"=>\&usage);
my $fname=shift;
usage if!$fname;

sub getsym {
	my($symbol)=@_;
	my $str=`$objdump -t $fname | grep $symbol`;
	die "symbol '$symbol' not found (objdump not working properly?)" if $?;
	my ($off)=$str=~/\s(\S+)\s+$symbol$/;
	$off = hex $off;
	open(my $f,$fname);
	seek $f,$off,0;

	my(@bts,$res);
	do {
		read $f,$str,22 or die "read failed";
		@bts=split//,$str;
		splice @bts,1,1;
		my $crc=0;
		map { $_=ord $_; $crc+=$_ } @bts;
		$crc=(-$crc)&0xff;
		($bts[1],$bts[2])=($bts[2],$bts[1]);
		$res.=':'.unpack('H*',pack('C'x($bts[0]+4),@bts).pack('C',$crc))."\n";
	} while($bts[3]!=0x01);

	close $f;

	return $res;
}

for(@symbols) {
	print STDERR "Extracting $_\n";
	open(my $f,'>',"$model$_.hex") or die "failed opening '$model$_' for writing: $!";
	print $f getsym($_);
	close $f;
}

open F,'>',"$model.rules" or die "failed opening '$model.rules' for writing: $!";
print F<<"E";
SUBSYSTEM=="usb", ACTION=="add", ENV{DEVTYPE}=="usb_device", ENV{PRODUCT}=="04b4/$product_id/*", RUN+="/sbin/fxload -t fx2 -I $firmware_dir/${model}_firmware.hex -s $firmware_dir/${model}_loader.hex -D \$env{DEVNAME}"
E
close F;
