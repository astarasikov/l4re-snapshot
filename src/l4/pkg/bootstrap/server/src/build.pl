#! /usr/bin/perl -W
#
# (c) 2008-2009 Technische Universität Dresden
# This file is part of TUD:OS and distributed under the terms of the
# GNU General Public License 2.
# Please see the COPYING-GPL-2 file for details.
#
# Adam Lackorzynski <adam@os.inf.tu-dresden.de>
#

use strict;

BEGIN { unshift @INC, $ENV{L4DIR}.'/tool/lib'
           if $ENV{L4DIR} && -d $ENV{L4DIR}.'/tool/lib/L4';}

use Digest::MD5;
use L4::ModList;


my $cross_compile_prefix = $ENV{CROSS_COMPILE} || '';
my $arch                 = $ENV{OPT_ARCH}     || "x86";

my $module_path  = $ENV{SEARCHPATH}   || ".";
my $prog_objcopy = $ENV{OBJCOPY}      || "${cross_compile_prefix}objcopy";
my $prog_cc      = $ENV{CC}           || "${cross_compile_prefix}gcc";
my $prog_ld      = $ENV{LD}           || "${cross_compile_prefix}ld";
my $prog_cp      = $ENV{PROG_CP}      || "cp";
my $prog_gzip    = $ENV{PROG_GZIP}    || "gzip";
my $compress     = $ENV{OPT_COMPRESS} || 0;
my $strip        = $ENV{OPT_STRIP}    || 1;
my $flags_cc     = "";
$flags_cc = "-m32" if $arch eq 'x86';
$flags_cc = "-m64" if $arch eq 'amd64';

my $make_inc_file = $ENV{MAKE_INC_FILE} || "mod.make.inc";

my $modulesfile      = $ARGV[0];
my $entryname        = $ARGV[1];

sub usage()
{
  print STDERR "$0 modulefile entry\n";
}

# Write a string to a file, overwriting it.
# 1:    filename
# 2..n: Strings to write to the file
sub write_to_file
{
  my $file = shift;

  open(A, ">$file") || die "Cannot open $file!";
  while ($_ = shift) {
    print A;
  }
  close A;
}

sub first_word($)
{
  (split /\s+/, shift)[0]
}

# build object files from the modules
sub build_obj($$$)
{
  my ($cmdline, $modname, $no_strip) = @_;
  my $_file = first_word($cmdline);

  my $file = L4::ModList::search_file($_file, $module_path)
    || die "Cannot find file $_file! Used search path: $module_path";

  printf STDERR "Merging image %s to %s\n", $file, $modname;
  # make sure that the file isn't already compressed
  system("$prog_gzip -dc $file > $modname.ugz 2> /dev/null");
  $file = "$modname.ugz" if !$?;
  system("$prog_objcopy -S $file $modname.obj 2> /dev/null")
    if $strip && !$no_strip;
  system("$prog_cp         $file $modname.obj")
    if $? || !$strip || $no_strip;
  my $uncompressed_size = -s "$modname.obj";

  my $c_unc = Digest::MD5->new;
  open(M, "$modname.obj") || die "Failed to open $modname.obj: $!";
  $c_unc->addfile(*M);
  close M;

  system("$prog_gzip -9f $modname.obj && mv $modname.obj.gz $modname.obj")
    if $compress;

  my $c_compr = Digest::MD5->new;
  open(M, "$modname.obj") || die "Failed to open $modname.obj: $!";
  $c_compr->addfile(*M);
  close M;

  write_to_file("$modname.extra.s",
      ".section .rodata.module_info               \n",
      ".align 4                                   \n",
      "_bin_${modname}_name:                      \n",
      ".ascii \"$_file\"; .byte 0                 \n",
      ".align 4                                   \n",
      "_bin_${modname}_md5sum_compr:              \n",
      ".ascii \"".$c_compr->hexdigest."\"; .byte 0\n",
      ".align 4                                   \n",
      "_bin_${modname}_md5sum_uncompr:            \n",
      ".ascii \"".$c_unc->hexdigest."\"; .byte 0  \n",
      ".align 4                                   \n",
      ".section .module_info                      \n",
      ".long _binary_${modname}_start             \n",
      ".long ", (-s "$modname.obj"), "            \n",
      ".long $uncompressed_size                   \n",
      ".long _bin_${modname}_name                 \n",
      ".long _bin_${modname}_md5sum_compr         \n",
      ".long _bin_${modname}_md5sum_uncompr       \n",
      ($arch eq 'x86' || $arch eq 'amd64' || $arch eq 'ppc32'
       ? #".section .module_data, \"a\", \@progbits   \n" # Not Xen
         ".section .module_data, \"awx\", \@progbits   \n" # Xen
       : ".section .module_data, #alloc           \n"),
      ".p2align 12                                \n",
      ".global _binary_${modname}_start           \n",
      ".global _binary_${modname}_end             \n",
      "_binary_${modname}_start:                  \n",
      ".incbin \"$modname.obj\"                   \n",
      "_binary_${modname}_end:                    \n",
      );
  system("$prog_cc $flags_cc -c -o $modname.bin $modname.extra.s");
  unlink("$modname.extra.s", "$modname.obj", "$modname.ugz");
}

sub build_mbi_modules_obj(@)
{
  my @mods = @_;
  my $asm_string;

  # generate mbi module structures
  $asm_string .= ".align 4                         \n".
                 ".section .data.modules_mbi       \n".
                 ".global _modules_mbi_start;      \n".
                 "_modules_mbi_start:              \n";

  for (my $i = 0; $i < @mods; $i++) {
    $asm_string .= ".long 0                        \n".
                   ".long 0                        \n".
		   ".long _modules_mbi_cmdline_$i  \n".
		   ".long 0                        \n";
  }

  $asm_string .= ".global _modules_mbi_end;        \n".
                 "_modules_mbi_end:                \n";

  $asm_string .= ".section .data.cmdlines          \n";

  # generate cmdlines
  for (my $i = 0; $i < @mods; $i++) {
    $asm_string .= "_modules_mbi_cmdline_$i:                  \n".
                   ".ascii \"$mods[$i]->{cmdline}\"; .byte 0; \n";
  }

  write_to_file("mbi_modules.s", $asm_string);
  system("$prog_cc $flags_cc -c -o mbi_modules.bin mbi_modules.s");
  unlink("mbi_modules.s");

}

sub build_objects(@)
{
  my %entry = @_;
  my @mods = @{$entry{mods}};
  my $objs = "mbi_modules.bin";
  
  unlink($make_inc_file);

  # generate module-names
  for (my $i = 0; $i < @mods; $i++) {
    $mods[$i]->{modname} = sprintf "mod%02d", $i;
  }

  build_mbi_modules_obj(@mods);

  for (my $i = 0; $i < @mods; $i++) {
    build_obj($mods[$i]->{cmdline}, $mods[$i]->{modname},
	      $mods[$i]->{type} =~ /.+-nostrip$/);
    $objs .= " $mods[$i]->{modname}.bin";
  }

  my $make_inc_str = "MODULE_OBJECT_FILES += $objs\n";
  $make_inc_str   .= "MOD_ADDR            := $entry{modaddr}"
    if defined $entry{modaddr};

  write_to_file($make_inc_file, $make_inc_str);
}

# ------------------------------------------------------------------------

if (!$ARGV[1]) {
  print STDERR "Error: Invalid usage!\n";
  usage();
  exit 1;
}

my %entry = L4::ModList::get_module_entry($modulesfile, $entryname);
build_objects(%entry);

