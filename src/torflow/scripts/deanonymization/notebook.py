import hidden_service_deanonymization

filenames = ['hs-times-1.dat', 'hs-times-2.dat']
(trials, times, trials_per_svc, circuits, circuits_per_svc) = \
    hidden_service_deanonymization.hidden_server_exp_data(filenames)
times = sorted(times)
max_time = times[-1]
median_time = times[len(times)/2-1] 
min_time = times[0]
mean_time = float(sum(times))/len(times)
percentile_99th_time = times[int(math.ceil(len(times)*0.99))]

### Calculate middle and guard probs and choose malicious relays ###
cons_filename = 'cached-consensus.hs1.fixed'
(middles_probs_names, guard_probs_names) =\
    hidden_service_deanonymization.middle_guard_probs(cons_filename)

### Get times until relay is selected as middle from hidden service ###
### and times until selected relay has observed all guards ###

# Relays and selection probabilities obtained using above
# Name  Middle selection probability    Guard selection probability
# relayguard91	0.00103116656105    0.00146884382993
# relayguard39	0.00261207176190    0.0037207621307
# relayguard16	0.00524366087196    0.00746932572191
# relayguard15	0.01010022767210    0.0143872558105
# relayguard24	0.02073392952720    0.0295344182081
# relayguard1	0.03007948640390    0.0428466842125

relays = ['relayguard91', 'relayguard39', 'relayguard16', 'relayguard15', 'relayguard24', 'relayguard1']
relay_probs_middle = [0.00103116656105, 0.0026120717619, 0.00524366087196, 0.0101002276721, .0207339295272, 0.0300794864039]
relay_probs_guard = [0.00146884382993, 0.0037207621307, 0.00746932572191, 0.0143872558105, 0.0295344182081, 0.0428466842125]

(counts_to_relay_selection, counts_to_all_guards) =\
    hidden_service_deanonymization.num_circuits_to_identify_guards(relays,
        circuits_per_svc, trials_per_svc)
    
# print statistics for each relay
print('Name\tSelection prob.\tAvg. # to sel\tMedian # to sel\tMin # to sel\tMax # to sel\t# selections\t# circuits')
for relay, prob in zip(relays, relay_probs_middle):
    counts = sorted(counts_to_relay_selection[relay])
    median = counts[int(0.5*(len(counts)-1))]
    avg = sum(counts)/float(len(counts))
    print('{}\t{:.5f}\t{:.5f}\t{}\t{}\t{}\t{}\t{}'.format(relay, prob, avg, median, counts[0], counts[-1], len(counts), num_circuits[relay]))    
print('Name\tSelection prob.\tAvg. # to guards\tMedian # to guards\tMin # to guards\tMax # to guards\t# times all guards')
for relay, prob in zip(relays, relay_probs_middle):
    counts = sorted(counts_to_all_guards[relay])
    if counts:
        median = counts[int(0.5*(len(counts)-1))]
        avg = sum(counts)/float(len(counts))
        print('{}\t{:.3f}\t{:.3f}\t{}\t{}\t{}\t{}'.format(relay, prob, avg, median, counts[0], counts[-1], len(counts)))
    else:
        print('{}\t{:.3f}\tN/A\tN/A\tN/A\tN/A\t{}'.format(relay, prob, 
            len(counts)))

    
### find Tor consensus bandwidths needed for guard and middle probabilities ###
ns_filename = '../../../torsec-metrics/torps.git/out/network-state/fat/network-state-2013-06/2013-06-30-23-00-00-network_state'
(max_obs_bws, regression_params) =\
    hidden_service_deanonymization.needed_cons_bw_for_probs(ns_filename,
    relay_probs_middle)
print('Relay\tNeeded cons BW\tMax obs BW\tCons BW for max obs BW\tMax cons BW')
for i in xrange(len(max_obs_bws)):
    needed_cons_bw = max_obs_bws[i][0]
    max_obs_bw = float(max_obs_bws[i][1])/1024
    max_obs_bw_cons_bw = max_obs_bws[i][2]
    max_cons_bw = max_obs_bws[i][3]
    print('{}\t{:<10.2f}\t{:<10.2f}\t{:<10}\t{:<10}'.\
        format(relays[i], needed_cons_bw, max_obs_bw, max_obs_bw_cons_bw,
            max_cons_bw))
######


### Sample distribution of times to kill all hidden-service guards ###
ns_filename = '../../../torsec-metrics/torps.git/out/network-state/slim/2013-06-30-23-00-00-network_state'
#ns_filename = 'out/network-state/slim/network-state-2013-06/2013-06-30-23-00-00-network_state'

# calculated above for this same consensus
# copied from above just for clarity
#relays = ['relayguard91', 'relayguard39', 'relayguard16', 'relayguard15', 'relayguard24', 'relayguard1']
needed_cons_bws = [9808.441185433607, 24885.37026010885, 50088.84196940566,
    96953.40122625131, 201188.9021638565, 294684.56029076426]

# regression parameters that Rob produced to calculate takedown rates
regression_slope = 9238.79545964
regression_intercept = -1499875.19796
#(consensus_weight - regression_intercept) / regression_slope =\
# minimum memory consumption rate observed in experiments in KiBps
min_rate = 152.864

# numbers calculated using above
circ_counts_to_all_guards = [None, 598.0, 357.333333333, 227.9375,
    141.735294118, 118.4]
# calculated using above
circ_construction_time = 10.691909535588875

# simultaneous circuits used for guard detection
parallel_circs = 10

# number of circuits to detect if relay chosen as guard
num_guard_detect_circs = 35

num_samples = 10000

(deanonymization_stats, bad_guard_probs) = hidden_service_deanonymization.\
    deanonymization_stats(ns_filename, needed_cons_bws, num_samples,
        circ_counts_to_all_guards, circ_construction_time, parallel_circs,
        regression_slope, regression_intercept, min_rate,
        num_guard_detect_circs)
        
# averages
avg_num_rounds = []
avg_time_1gb = []
avg_time_4gb = []
avg_time_8gb = []
avg_num_guards_killed = []
for relay_deanonymizations_stats in deanonymization_stats:
    num_rounds = map(lambda x: x['num_rounds'], relay_deanonymizations_stats)
    avg_num_rounds.append(float(sum(num_rounds))/len(num_rounds))
    time_1gb = map(lambda x: x['time_1gb']/3600.0,
        relay_deanonymizations_stats)
    avg_time_1gb.append(float(sum(time_1gb))/len(time_1gb))
    time_4gb = map(lambda x: x['time_4gb']/3600.0,
        relay_deanonymizations_stats)
    avg_time_4gb.append(float(sum(time_4gb))/len(time_4gb))
    time_8gb = map(lambda x: x['time_8gb']/3600.0,
        relay_deanonymizations_stats)
    avg_time_8gb.append(float(sum(time_8gb))/len(time_8gb))
    num_guards_killed = map(lambda x: x['num_guards_killed'],
        relay_deanonymizations_stats)
    avg_num_guards_killed.append(\
        float(sum(num_guards_killed))/len(num_guards_killed))

######

##### SCRATCH #####
##########