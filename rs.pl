#!/usr/bin/perl -w

use strict;
use warnings;
use IO::Socket;

# Use our local library.
# use lib ".";

# Case-based reasoning
use AI::CBR::Sim qw(sim_set sim_eq);
use AI::CBR::Case;
use AI::CBR::Retrieval;

# Sentence parser
use Lingua::LinkParser;

# Load RiveScript.
use RiveScript;

print "Initializing sentence parser and case-based reasoning\n";
our $parser = new Lingua::LinkParser;
our @cases = {};

# Create and load the RiveScript brain.
print "Initializing RiveScript interpreter\n";
our $rs = new RiveScript;
$rs->loadDirectory ("./replies");
$rs->sortReplies;

my $socket = "/tmp/rs";
unlink $socket;
print "Initializing socket\n";
my $server = IO::Socket::UNIX->new(Local => $socket,
				   Type => SOCK_STREAM,
				   Listen => SOMAXCONN) or die $@;
chmod(0777, $socket) || die $!;

print "Initialization complete.\n";

# Start.
while (1) {
	my $client = $server->accept();
	my $buf;
	$client->recv($buf, 1024);
	my ($who, $msg) = split(/\007/, $buf);
	print $who . ': ' . $msg . "\n";
	my $reply = '';
	my @msg_array = split(/[\.\?\!]/, $msg);
	$rs->thawUservars($who);
	foreach (@msg_array) {
		if ($_ =~ /[a-zA-Z0-9]/) {
			my $treply = $rs->reply($who, $_);

			my $said = $rs->{client}->{$who}->{__history__}->{input}->[0];
			my $sentence = $parser->create_sentence($said);
			my @linkages = $sentence->linkages;

			foreach my $linkage (@linkages) {
				print $parser->get_diagram($linkage);
			}

			my $case = AI::CBR::Case->new(
				said	=> {
					sim	=> \&sim_eq
				},
				words	=> {
					sim	=> \&sim_set
				},
			);
			$case->set_values(
				said	=> $said,
				words	=> [ split(/\s+/, $said) ],
			);

			my $r = AI::CBR::Retrieval->new($case, \@cases);
			$r->compute_sims();
			my $solution = $r->most_similar_candidate();
			print $solution->{_sim} * 100 . "% similarity\n";

			if ($treply ne 'generate') {
				if ($solution->{_sim} ne 1) {
					my $new_case = {
						said	=> $said,			
						words	=> [ split(/\s+/, $said) ],
						isaid	=> $treply,
					};
					push @cases, $new_case;
				}
				$reply .= $treply . '  ';
			} else {
				if ($solution->{_sim} ne 0) {
					$reply .= $solution->{isaid} . '  ';
				} else {
					$reply .= $rs->reply($who, 'random pickup line') . '  ';
				}
			}
		}
	}
	if ($reply =~ /^$/) {
		$reply = ":)";
	}
	$rs->freezeUservars($who);
	$client->send($reply);
	print 'me: ' . $reply . "\n---" . time . "\n";
	$client->close;
}
