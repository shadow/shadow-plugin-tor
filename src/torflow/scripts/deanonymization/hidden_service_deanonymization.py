import sys
# hack to avoid creating another installation of TorPS
sys.path.insert(0, '../../../torsec-metrics/torps.git')
import pathsim

import os, json
import math
import stem, stem.descriptor
import pickle
from pathsim import RouterStatusEntry
from pathsim import NetworkStatusDocument
from pathsim import ServerDescriptor
import network_analysis

def hidden_server_exp_data(filenames):
    """Process concatenated data from different experiments."""
    trials = [] # aggregate trials
    times = [] # times from client initiation to server pattern detection
    trials_per_svc = {} # divide trials by hidden service
    circuits = [] # circuits observed
    circuits_per_svc = {}
    for filename in filenames:
        offset = len(trials_per_svc)
        with open(filename) as f:
            data = json.load(f)
        # get times
        for trial in data:
            trials.append(trial)
            time = trial[3] - trial[1]
            if (time >= 0):
                times.append(time)
            else:
                print('Unexpected negative time: {}'.format(time))
        # divide trials by hs
        for trial in data:
            svcid = trial[0] + offset
            if svcid in trials_per_svc:
                trials_per_svc[svcid].append(trial)
            else:
                trials_per_svc[svcid] = [trial]
        # Read in circuits built
        for trial in data:
            circuits.append(trial[4])
        # divide circuits by hs
        for trial in data:
            svcid = trial[0] + offset
            if svcid in circuits_per_svc:
                circuits_per_svc[svcid].append(trial[4])
            else:
                circuits_per_svc[svcid] = [trial[4]]
        return (trials, times, trials_per_svc, circuits, circuits_per_svc)

def node_probs(nodes, weights):
    """Takes list of nodes (rel_stats) and weights (as a dict) and outputs
    a list of (node, prob) pairs, where prob is the probability of the nodes
    weighted by weights."""
    # compute total weight
    total_weight = 0
    for node in nodes:
        total_weight += weights[node]
    if (total_weight == 0):
        raise ValueError('ERROR: Node list has total weight zero.')
    # create cumulative weights
    node_probs = [(node, float(weights[node])/total_weight) for \
        node in nodes]
    return node_probs


def middle_guard_probs(cons_filename):
    """Calculate selection probabilites as middles and guards.
    Will be useful to find relays to consider as malicious
    during Shadow experiments."""
    # read in experiment consensuses
    # ripped from pathsim.process_consensuses    
    cons_f = open(cons_filename, 'rb')
    cons_valid_after = None
    cons_fresh_until = None
    cons_bw_weights = None
    cons_bwweightscale = None
    consensus = None
    for r_stat in stem.descriptor.parse_file(cons_f,
        'network-status-consensus-3 1.0', validate=True):        
        if (cons_valid_after == None):
            cons_valid_after = r_stat.document.valid_after
            # compute timestamp version once here
        if (cons_fresh_until == None):
            cons_fresh_until = r_stat.document.fresh_until
            # compute timestamp version once here
        if (cons_bw_weights == None):
            cons_bw_weights = r_stat.document.bandwidth_weights
        if (cons_bwweightscale == None) and \
            ('bwweightscale' in r_stat.document.params):
            cons_bwweightscale = r_stat.document.params[\
                    'bwweightscale']
        if (consensus == None):
            consensus = r_stat.document
            consensus.routers = {} # should be empty - ensure
        consensus.routers[r_stat.fingerprint] = r_stat        
    cons_f.close()
    # create dummy descriptor dictionary for pathsim functions
    descriptors = {}
    for fprint in consensus.routers:
        descriptors[fprint] = True
    # Calculate middle and guard probabilities

    # calculate middle probabilities
    middles = consensus.routers.keys()
    middle_weights = pathsim.get_position_weights(middles, consensus.routers,
        'm', cons_bw_weights, cons_bwweightscale)
    middle_probs = node_probs(middles, middle_weights)
    middle_probs = sorted(middle_probs, key = lambda x: x[1], reverse = True) 
    # add in names to fingerprints and middle probabilities
    middles_probs_names = []
    for mp in middle_probs:
        middles_probs_names.append((mp[0], consensus.routers[mp[0]].nickname,
            mp[1]))

    # calculate guard probabilities
    guards = pathsim.filter_guards(consensus.routers, descriptors)
    guards = filter(lambda x: (stem.Flag.FAST in consensus.routers[x].flags),
        guards)
    guard_weights = pathsim.get_position_weights(guards, consensus.routers,
        'g', cons_bw_weights, cons_bwweightscale)
    guard_probs = node_probs(guards, guard_weights)    
    guard_probs = sorted(guard_probs, key = lambda x: x[1], reverse = True)
    guard_probs_names = []
    for gp in guard_probs:
        guard_probs_names.append((gp[0], consensus.routers[gp[0]].nickname,
            gp[1]))
            
    return (middles_probs_names, guard_probs_names)
    
def num_circuits_to_identify_guards(relays, circuits_per_svc, trials_per_svc):
    """
    Takes malicious relays and processed data from hidden service experiments.
    Returns counts until relay is first selected and until all guards observed.
    """

    # filter services for those that do not use relay as guard
    counts_to_relay_selection = {}
    counts_to_all_guards = {}
    num_circuits = {} # circuits created by service not using relay as guard
    for relay in relays:
        svc_guards = {}
        for svc in circuits_per_svc:
            svc_guards[svc] = set()
            for circuit in circuits_per_svc[svc]:
                svc_guards[svc].add(circuit[0])
        svcs_wo_guard = filter(lambda x: relay not in svc_guards[x],
            svc_guards.keys())
        # circs until relay chosen as middle and until has observed all guards
        cnts_to_sel = []
        cnts_to_guards = []
        num_circuits[relay] = 0
        for svc in svcs_wo_guard:
            # make sure circuits ordered by start time
            svc_trials = sorted(trials_per_svc[svc], key = lambda x: x[1])
            num_circuits[relay] += len(svc_trials)
            cnt_to_sel = 0
            cnt_to_guards = 0
            unobs_guards = svc_guards[svc].copy()
            for trial in svc_trials:
                cnt_to_sel += 1
                cnt_to_guards += 1
                if (trial[4][1] == relay):
                    # add count to selection as middle
                    cnts_to_sel.append(cnt_to_sel)
                    cnt_to_sel = 0
                    # detect new guard observation
                    if (trial[4][0] in unobs_guards):
                        unobs_guards.remove(trial[4][0])
                        if (not unobs_guards): # all guards seen
                            cnts_to_guards.append(cnt_to_guards)
                            unobs_guards = svc_guards[svc].copy()
                            cnt_to_guards = 0
        counts_to_relay_selection[relay] = cnts_to_sel
        counts_to_all_guards[relay] = cnts_to_guards
    return (counts_to_relay_selection, counts_to_all_guards)
    
def needed_cons_bw_for_probs(ns_filename, relay_probs_middle):
    """
    Takes middle probabilities and TorPS network state file.
    Returns Tor consensus bandwidth needed for desired middle probability.
    """
    # reading in fat network state file
    # thus cannot use pathsim.get_network_state()
    cons_rel_stats = {}
    with open(ns_filename, 'rb') as nsf:
        consensus = pickle.load(nsf)
        descriptors = pickle.load(nsf)  
    # set variables from consensus
    cons_valid_after = pathsim.timestamp(consensus.valid_after)            
    cons_fresh_until = pathsim.timestamp(consensus.fresh_until)
    cons_bw_weights = consensus.bandwidth_weights
    if ('bwweightscale' not in consensus.params):
        cons_bwweightscale = pathsim.TorOptions.default_bwweightscale
    else:
        cons_bwweightscale = consensus.params['bwweightscale']
    for relay in consensus.routers:
        if (relay in descriptors):
            cons_rel_stats[relay] = consensus.routers[relay]
            
    # get list of potential middle relays with selection probabilities
    fast = True
    stable = False
    middles = filter(lambda x: pathsim.middle_filter(x, cons_rel_stats,
        descriptors, fast, stable, None, None), cons_rel_stats.keys())
    # create cumulative weighted middles
    middle_weights = pathsim.get_position_weights(middles, cons_rel_stats, 'm',
        cons_bw_weights, cons_bwweightscale)
    middle_probs = node_probs(middles, middle_weights)
    middle_probs = sorted(middle_probs, key = lambda x: x[1], reverse = True)
    total_middle_weight = sum(map(lambda x: middle_weights[x], middles))

    # consider max cons bw needed for middle prob
    # use largest observed bw for relays with at most that cons bw
    relay_flags = [stem.Flag.GUARD]
    middle_weight = float(pathsim.get_bw_weight(relay_flags, 'm',
        cons_bw_weights)) / float(cons_bwweightscale)
    needed_cons_bws = []
    for middle_prob in relay_probs_middle:
        needed_middle_cons_bw = float(middle_prob) * total_middle_weight /\
            ((1-middle_prob) * middle_weight)
        needed_cons_bws.append(needed_middle_cons_bw)
    
    # look at maximum observed bw for relay with at most needed cons bw
    max_obs_bws = []
    for needed_cons_bw in needed_cons_bws:
        max_obs_bw = 0
        max_obs_bw_cons_bw = 0
        max_cons_bw = 0
        for fp, rel_stat in cons_rel_stats.items():
            cons_bw = rel_stat.bandwidth
            if (cons_bw <= needed_cons_bw):
                obs_bw = descriptors[fp].observed_bandwidth
                if (obs_bw > max_obs_bw):
                    max_obs_bw = obs_bw
                    max_obs_bw_cons_bw = cons_bw
                max_cons_bw = max(max_cons_bw, cons_bw)
        max_obs_bws.append((needed_cons_bw, max_obs_bw, max_obs_bw_cons_bw,
            max_cons_bw))
            
    # also look at results of regression on obs/avg bw and cons bw
    middle_estimated_bws = []
    middle_cons_bws = []
    for middle in middles:
        rel_stat = cons_rel_stats[middle]
        desc = descriptors[middle]
        estimated_bw = min(desc.observed_bandwidth, desc.average_bandwidth)
        middle_estimated_bws.append(estimated_bw)
        middle_cons_bws.append(rel_stat.bandwidth)
    regression_params = network_analysis.linear_regression(middle_cons_bws,
        middle_estimated_bws)
    #regression_params = (a, b, r_squared)
    return (max_obs_bws, regression_params)
    
def guards_killed_during_deanonymization(needed_cons_bws, num_samples,
    cons_bw_weights, cons_bwweightscale, cons_rel_stats, descriptors):
    """Returns simulation samples of guards killed during deanonymization Dos 
    attack."""
    
    # malicious guard properties
    bad_fp = '0' * (40-1) + '1'
    bad_nickname = 'BadGuyGuard'
    bad_flags = [stem.Flag.FAST, stem.Flag.GUARD, stem.Flag.RUNNING,
        stem.Flag.STABLE, stem.Flag.VALID]
    bad_hibernating = False
    bad_family = {}
    bad_address = '10.0.0.0'
    bad_exit_policy = stem.exit_policy.ExitPolicy('reject *:*')
    
    # go through each added malicious relay and sample deanonymization times
    guards_killed = []
    for malicious_cons_bw in needed_cons_bws:
        # add malicious relay to consensus and descriptors
        cons_rel_stats[bad_fp] = pathsim.RouterStatusEntry(bad_fp,
            bad_nickname, bad_flags, malicious_cons_bw)
        descriptors[bad_fp] = pathsim.ServerDescriptor(bad_fp,
            bad_hibernating, bad_nickname, bad_family, bad_address,
            bad_exit_policy)
                    
        # precompute weighted guards list as done in pathsim.create_circuits()
        potential_guards = pathsim.filter_guards(cons_rel_stats, descriptors)
        potential_guard_weights =\
            pathsim.get_position_weights(potential_guards,
                cons_rel_stats, 'g', cons_bw_weights, cons_bwweightscale)
        weighted_guards = pathsim.get_weighted_nodes(potential_guards,
            potential_guard_weights)
    
        guards_killed_samples = []
        for i in xrange(num_samples):
            # do initial guard selection (not including malicious relay)
            # guards actually chosen using get_guards_for_circ().
            # requires exit though, which we are skipping
            # weird double loop replicates Tor guard selection method
            guards_killed_sample = []
            dead_guards = set()
            client_guards = []
            for j in xrange(pathsim.TorOptions.num_guards):
                while True:
                    guard = pathsim.get_new_guard(cons_bw_weights,
                        cons_bwweightscale, cons_rel_stats, descriptors,
                        client_guards, weighted_guards)
                    # assume guards selected before malicious relay inserted
                    if (guard != bad_fp):
                        break
                if (stem.Flag.FAST in cons_rel_stats[guard].flags):
                    client_guards.append(guard)
            # now add extra guards if not enough FAST ones selected
            while (len(client_guards) < pathsim.TorOptions.min_num_guards):
                guard = pathsim.get_new_guard(cons_bw_weights,
                    cons_bwweightscale, cons_rel_stats, descriptors,
                    client_guards, weighted_guards)
                if (stem.Flag.FAST in cons_rel_stats[guard].flags):
                    client_guards.append(guard)
                    
            # kill guards; do guard reselection until malicious guard selected
            while (bad_fp not in client_guards):
                guards_killed_sample.append(client_guards)
                dead_guards.update(client_guards)
                client_guards = []
                # add enough new active guards
                while (len(client_guards) < pathsim.TorOptions.min_num_guards):
                    guard = pathsim.get_new_guard(cons_bw_weights,
                        cons_bwweightscale, cons_rel_stats, descriptors,
                        client_guards, weighted_guards)
                    if (stem.Flag.FAST in cons_rel_stats[guard].flags) and\
                        (guard not in dead_guards):
                        client_guards.append(guard)
            guards_killed_samples.append(guards_killed_sample)
        guards_killed.append(guards_killed_samples)        
        # remove added malicious relay for next malicious relay
        del cons_rel_stats[bad_fp]
        del descriptors[bad_fp]
        
    return guards_killed
    
    
def deanonymization_stats_from_guards(guards_killed, cons_rel_stats,
    circ_counts_to_all_guards, circ_construction_time, parallel_circs,
    regression_slope, regression_intercept, min_rate, num_guard_detect_circs):
    """
    Takes sampled guards killed during simulations of various malicious relays.
    Turns those into useful statistics about speed of attack.
    """
    # turn guards killed into deanonymization times
    deanonymization_stats = []
    for guards_killed_samples, circ_cnts in zip(guards_killed,
        circ_counts_to_all_guards):
        deanonymization_stats_samples = []
        for guards_killed_sample in guards_killed_samples:
            deanonymization_stats_sample = {'num_rounds':0, 'time_1gb':0,
                'time_4gb':0, 'time_8gb':0, 'num_guards_killed':0}
            for guards in guards_killed_sample:
                deanonymization_stats_sample['num_rounds'] += 1
                deanonymization_stats_sample['num_guards_killed'] +=\
                    len(guards)
                # add time to discover all guards using given malicious relay
                if (circ_cnts != None): # skip detection time if circ_cnt=None
                    deanonymization_stats_sample['time_1gb'] +=\
                        float(circ_cnts) * circ_construction_time /\
                        parallel_circs
                    deanonymization_stats_sample['time_4gb'] +=\
                        float(circ_cnts) * circ_construction_time /\
                        parallel_circs
                    deanonymization_stats_sample['time_8gb'] +=\
                        float(circ_cnts) * circ_construction_time /\
                        parallel_circs
                # add time to snipe all guards in parallel
                min_guard_cons_bw = min(map(lambda x:\
                    cons_rel_stats[x].bandwidth, guards))
                # scale by 1000 bc regression was done with such scaling
                mem_consume = max((1000.0*min_guard_cons_bw -\
                regression_intercept) / regression_slope, min_rate)             
                    
                deanonymization_stats_sample['time_1gb'] += 1.0*(1024**2) /\
                    mem_consume
                deanonymization_stats_sample['time_4gb'] += 4.0*(1024**2) /\
                    mem_consume
                deanonymization_stats_sample['time_8gb'] += 8.0*(1024**2) /\
                    mem_consume
                # add time to detect if chosen as guard
                deanonymization_stats_sample['time_1gb'] +=\
                    circ_construction_time *\
                    math.ceil(num_guard_detect_circs/parallel_circs)
                deanonymization_stats_sample['time_4gb'] +=\
                    circ_construction_time *\
                    math.ceil(num_guard_detect_circs/parallel_circs)
                deanonymization_stats_sample['time_8gb'] +=\
                    circ_construction_time *\
                    math.ceil(num_guard_detect_circs/parallel_circs)
            deanonymization_stats_samples.append(deanonymization_stats_sample)
        deanonymization_stats.append(deanonymization_stats_samples)  
        
    return deanonymization_stats

    
def deanonymization_stats(ns_filename, needed_cons_bws, num_samples,
    circ_counts_to_all_guards, circ_construction_time, parallel_circs,
    regression_slope, regression_intercept, min_rate, num_guard_detect_circs):
    """
    Takes Tor network state file, parameters for attack phases,
    sizes of malicious relay, and number of samples to return.
    Returns samples of time-related statistics for deanonymization DoS attack.
    """
    
    (cons_valid_after, cons_fresh_until, cons_bw_weights,
        cons_bwweightscale, cons_rel_stats, hibernating_statuses,
        descriptors) = pathsim.get_network_state(ns_filename, 0, {}) 
        
    # determine guard selection probability for malicious relay
    bad_guard_probs = []
    for needed_cons_bw in needed_cons_bws:
        potential_guards = pathsim.filter_guards(cons_rel_stats, descriptors)
        # add further FAST filtering applied later in path selection
        potential_guards = filter(lambda x:\
            stem.Flag.FAST in cons_rel_stats[x].flags, potential_guards)
        potential_guard_weights =\
            pathsim.get_position_weights(potential_guards,
                cons_rel_stats, 'g', cons_bw_weights, cons_bwweightscale)
        total_guard_weight = sum(potential_guard_weights.values())
        bad_flags = [stem.Flag.FAST, stem.Flag.GUARD, stem.Flag.RUNNING,
            stem.Flag.STABLE, stem.Flag.VALID]
        bad_node_wt = needed_cons_bw * float(pathsim.get_bw_weight(bad_flags,
            'g', cons_bw_weights)) / float(cons_bwweightscale)
        bad_guard_probs.append(bad_node_wt / (total_guard_weight+bad_node_wt))
    
    guards_killed = guards_killed_during_deanonymization(needed_cons_bws,
        num_samples, cons_bw_weights, cons_bwweightscale, cons_rel_stats,
        descriptors)
    stats = deanonymization_stats_from_guards(guards_killed, cons_rel_stats,
        circ_counts_to_all_guards, circ_construction_time, parallel_circs,
        regression_slope, regression_intercept, min_rate,
        num_guard_detect_circs)
    return (stats, bad_guard_probs)