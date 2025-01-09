#!/usr/bin/env python3

import requests

#url_base = 'http://127.0.0.1:38157/'
url_base = 'http://127.0.0.1:35719/'

def json_rpc(method, params=None, timeout=50):
    json = {
        "jsonrpc": "2.0",
        "id": "0",
        "method": method,
    }
    if params:
        json["params"] = params

    return requests.post('{}/json_rpc'.format(url_base), json=json, timeout=timeout)

def get_service_nodes():
    return json_rpc('get_service_nodes').json()

addresses = {
    'T6TSSZFiy74HzC41GNtXP4RLECdbV2YFsQ3fDbZGyF3mgHh5FoEia593CYAmsfzRsub2nXsB1xK7rFbWgW7dTmgf1eFvt7Mya': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6Sc6yPqH75FX8R7ENGiksMA2oJkEzH985i4gQVeoUpw4ERTG3yuzpghGRbjZ8REh1bpqq4qE8Nut25bAcd9npJL283WQ3o9d': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6SzmUAGfmaJQMtX8jxTLaGJgzXD6YLiePgGmg9PS3YMCcfvEmWUt8sadoZQkDhtamiEFX4t6tdjh2rKcSc3Hugr2zV9EvdeB': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6TXLNZL3Hjg8VK3NfaGPqAH38a6T7BRKMNTKQ8ZaVUGPaNNG9m3MKs7DVC29VMdZMWD1EQrVSqkUGamyjmb5ZRj2h5D6mEac': '0xb82Cd271CE0E498e4203AC4db801698Bd720f6AF',
    'T6TxKpWqmokjGRf2wvX9cMegXcywCA8aiFq9UEHeFjpBiHLiKr2q3xnR1RsE5ky6UDNhRpWm7Vd1hZYEpeLofA5P1fAxML1eD': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6TwBFTzXcQe4qG3PNZTv54s9cC9ber4zEcjaTK4Z2ty36wiA5cPCNW5HvZE4wt2i97B417MmRLQ6gRSShNGj4J72aHRb9CL2': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6Swaxm6LGsgSgXJSMwf5pEbCk7adnMbeTs3z9PNoj98LLxZaUNKEtyYF3hKaHrJY9U4XBP1UnsfuVBu87zL5gXr21qq6eKKj': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6THE6fvpP3aozo7DbH6uu86cfwHPQKUxUcRwSYWD8HX5ygcRefFkyti2n9kDMJEoUNafEnPTNVsiQARCjno8MqG1mu2eCFQG': '0xb82Cd271CE0E498e4203AC4db801698Bd720f6AF',
    'T6SRX3ZRd9V8EAtfycowxMCrtLeEVt5vxe4cCyfcofAkGHcTnbQbyZzHjJ4syqAaiR4ZhuhBynubQQJ2gMQFAAxg1ByvT6KRu': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6U7YGUcPJffbaF5p8NLC3VidwJyHSdMaGmSxTBV645v33CmLq2ZvMqBdY9AVB2z8uhbHPCZSuZbv68hE6NBXBc51Gg9MGUGr': '0xB0CefD61ddB88176Fb972955341adC6c1d05230e',
    'T6UAgQc8sfR5aDXbcuX5fDetbLQpzfaaDVkz6C87ST9HZ35V9gpnwQ5JVVX5AAe9QhBYzEnqBZcHQhqPwrYHyuoV39DmpNvyJ': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6TgN5JWZYP8bzvju2sPnHZi79xT3z3UZQb1EZ7jnV8HSME5jCCn74LZZWtaKHtxqK8Sd8xXnVBxg9t9vgZL1muK35iWjZQei': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6TQtW85azZD4aZYE7yjUsbPCH6FCP2G35sT3xUJSckjeTWvqzex7PRHxg8QzFRfTKV3EVLEmUtQWavsV7rLCwdJ1vHiwtE9r': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6SDxPqij1SCVuQngE1gcvTB5hewtHaGfX3N7S8fq3bfNCx3VuKhgbQcwJC2d4euWc5HcgFfFD2Kb575YDMQej7p2XgzC7n5R': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6SzBDzFwNxf81jjc3fB7nhFKSRr8ZJ5pCNtEsxz1vYNSGrmpBMxNZGYZKYpajvmRwbDj2ciHUnG49K3RHb3UCRS1FHukhN9H': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6TRcVzjAXiF4VBpxq5Yfk7mNJ7nPSVYFGR3zR7afdPBY63yKSzSm1Ca9NP4Q2suJUSF2pxhMeqKuJcbphVuCUDD1wsasYWpL': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6SgE3UwuB773QHVq7NDQ4QRNRyezGSBFTnPGZRz6H1T2XogQEG3pQ4b83AD1savr42r3j56UP16k58XmP3GWq8o2evtxsDHY': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6SMzKQ5S9KBwdB7ehu43PWMQDhcZjKGcfqBbaCCvzmgCTHPuPxLc9BNAccsnHKguiHYkBb8VwwxYPnBy5pN29Q42tadfBTQL': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6UCruB8Zovcyrbk8g3PMDPJEs6fC3XZwZ2RUY4iKjoChL5FZNEht953nYCxyeP5VmcZXSHXnN3Yi7sXLw6aRgCc14hzzqZ1a': '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e',
    'T6SJdAiu1RUejh1XNgPH3yGE2pXBin3iQJcMFxAi6BrbC9fhNSBUpcGgZ1JZnacn7fKTJhom3wspHJtSwWspX1bM2sj92ZFkx': '0xb82Cd271CE0E498e4203AC4db801698Bd720f6AF',
    'T6Si1Nx6W1Wd9PMYZmJUzBBzzehhiqAmoSmotUbzoiaQTyhSYQNsmP1LsDGofwRyiUVrLPgTZTgkQVF52uMjNCq32huT1aVAJ': '0xb82Cd271CE0E498e4203AC4db801698Bd720f6AF',
    'T6SoZfYnck8id92aLQP3DBbp8ugBXnMCvaiBbD5dbqkp9n4DW1zZFjfGSKPbfpAXQ8H9bskNQgrcAFm81qhy2dSs2UBVrvJ7g': '0xb82Cd271CE0E498e4203AC4db801698Bd720f6AF',
    'T6SrF7SW4bh9uGSSUPT7MQKimYt9rhXuqS7Ujabo6KdX5w4AatFZcKoQeo3jfbwkUEdZAjayeKa5x9QejrWSMJvi2PYqNEwcq': '',
}

transition_bonus = {
    '0xB0CefD61ddB88176Fb972955341adC6c1d05230e': 123451234512,
    '0xB7649B5A5DfABAA0713ACFB3040945035b0bBD9e': 33334444222,
    '0xb82Cd271CE0E498e4203AC4db801698Bd720f6AF': 4206900,
}

conversion_ratio = 200 # testnet SENT stake is 200x testnet OXEN stake
staking_requirement = 20000000000000

# added for testnet to get all contributor addresses to put in the
# list above, as I did not have a list.
def get_addresses():

    addrs = {}
    for res in get_service_nodes()['result']['service_node_states']:
        for contributor in res['contributors']:
            addrs[contributor['address']] = 1

    for addr in addrs:
        print(addr)

def get_migration():
    bls_map = {}
    edkey_map = {}
    seed_list = []
    for res in get_service_nodes()['result']['service_node_states']:
        edkey = res['service_node_pubkey']

        if not res['active']:
            print(f"Not migrating {edkey} because it's not active")
            continue

        if 'pubkey_bls' not in res:
            print(f"Not migrating {edkey} because it somehow does not have a bls pubkey set")
            continue

        if res['operator_address'] not in addresses:
            print(f"Not migrating {edkey} because its operator address is not registered to convert")
            continue

        if res['operator_address'] != res['contributors'][0]['address']:
            print(f"operator_address != first contributor address -- ({res['operator_address']} != {res['contributors'][0]['address']}")
            continue

        ok = True
        contributors = []
        contribution_sum = 0
        for cont in res['contributors']:
            if cont['address'] not in addresses or len(addresses[cont['address']]) == 0:
                ok = False
                break
            contributors.append({'address': addresses[cont['address']], 'amount': cont['amount'] * conversion_ratio})
            contribution_sum += cont['amount'] * conversion_ratio

        if not ok:
            print(f"Not migrating {edkey} because a contributor address is not registered to convert")
            continue

        # TODO: normalize if sum > req for the few nodes on mainnet with a higher stake sum
        if contribution_sum != staking_requirement:
            print(f"Stakes * conversion_ratio != staking_requirement, {contribution_sum} != {staking_requirement}")
            continue

        seed_list.append({
            'bls_pubkey': res['pubkey_bls'],
            'ed25519_pubkey': edkey,
            'contributors': contributors,
        })

        bls_map[edkey] = res['pubkey_bls']

        if res['service_node_pubkey'] != edkey:
            edkey_map[res['service_node_pubkey']] = edkey

    return [bls_map, edkey_map, seed_list]

def print_migration():
    bls_map, edkey_map, seed_list = get_migration()

    print("Printing C++ for oxen -> eth mapping\n")
    for addr in addresses:
        if len(addresses[addr]):
            print(f"{{\"{addr}\"s, tools::make_from_hex_guts<eth::address>(\"{addresses[addr]}\"s)}},")
    print("")

    print("Printing C++ for monero key -> ed25519 key mapping\n")
    for monero_key in edkey_map:
        print(f"{{tools::make_from_hex_guts<crypto::public_key>(\"{monero_key}\"s), tools::make_from_hex_guts<crypto::ed25519_public_key>(\"{edkey_map[monero_key]}\"s)}},")
    print("")

    print("Printing C++ for ed -> bls mapping\n")
    for edkey in bls_map:
        print(f"{{tools::make_from_hex_guts<crypto::ed25519_public_key>(\"{edkey}\"s), tools::make_from_hex_guts<eth::bls_public_key>(\"{bls_map[edkey]}\"s)}},")
    print("")

    print("Printing C++ for transition bonus\n")
    for addr in transition_bonus:
        print(f"{{tools::make_from_hex_guts<eth::address>(\"{addr}\"s), {transition_bonus[addr]}}},")
    print("")

    print("Printing python for seeding contract\n")
    from pprint import pp
    pp(seed_list)

print_migration()
#get_addresses()
