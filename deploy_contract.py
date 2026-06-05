#!/usr/bin/env python3
"""
Script để deploy DrowsinessDetection.sol lên Sepolia testnet
Yêu cầu: pip install web3 python-dotenv
"""

from web3 import Web3
from web3.middleware import ExtraDataToPOAMiddleware
import os
import json
from dotenv import load_dotenv
from solcx import compile_source, install_solc, set_solc_version

# Load environment variables
load_dotenv()

# Configuration
INFURA_URL = os.environ.get('INFURA_URL')
PRIVATE_KEY = os.environ.get('PRIVATE_KEY')

if not INFURA_URL or not PRIVATE_KEY:
    print("❌ Thiếu INFURA_URL hoặc PRIVATE_KEY trong .env file")
    exit(1)

SOLIDITY_SOURCE = None
CONTRACT_ABI = None
CONTRACT_BYTECODE = None

def load_compiled_contract():
    global SOLIDITY_SOURCE, CONTRACT_ABI, CONTRACT_BYTECODE
    if CONTRACT_ABI and CONTRACT_BYTECODE:
        return CONTRACT_ABI, CONTRACT_BYTECODE

    with open('DrowsinessDetection.sol', 'r', encoding='utf-8') as solidity_file:
        SOLIDITY_SOURCE = solidity_file.read()

    install_solc('0.8.20')
    set_solc_version('0.8.20')

    compiled = compile_source(
        SOLIDITY_SOURCE,
        output_values=['abi', 'bin']
    )
    contract_id, contract_interface = next(iter(compiled.items()))
    CONTRACT_ABI = contract_interface['abi']
    CONTRACT_BYTECODE = contract_interface['bin']
    return CONTRACT_ABI, CONTRACT_BYTECODE

def deploy_contract():
    """Deploy smart contract lên Sepolia"""
    print("🚀 Deploying DrowsinessDetection contract to Sepolia...")

    # Khởi tạo Web3
    web3 = Web3(Web3.HTTPProvider(INFURA_URL))
    web3.middleware_onion.inject(ExtraDataToPOAMiddleware, layer=0)

    if not web3.is_connected():
        print("❌ Cannot connect to Ethereum network")
        return None

    # Account setup
    account = web3.eth.account.from_key(PRIVATE_KEY)
    print(f"📧 Deploying from account: {account.address}")

    # Check balance
    balance = web3.eth.get_balance(account.address)
    balance_eth = web3.from_wei(balance, 'ether')
    print(f"💰 Account balance: {balance_eth} ETH")

    if balance < web3.to_wei(0.01, 'ether'):
        print("❌ Insufficient funds. Need at least 0.01 ETH for deployment")
        print("   Get test ETH from: https://sepoliafaucet.com/")
        return None

    # Deploy contract
    try:
        abi, bytecode = load_compiled_contract()

        # Tạo contract instance
        DrowsinessContract = web3.eth.contract(abi=abi, bytecode=bytecode)

        # Build transaction
        nonce = web3.eth.get_transaction_count(account.address)
        gas_price = web3.eth.gas_price

        txn = DrowsinessContract.constructor().build_transaction({
            'chainId': 11155111,  # Sepolia
            'gas': 3000000,
            'gasPrice': gas_price,
            'nonce': nonce,
        })

        # Sign and send
        signed_txn = web3.eth.account.sign_transaction(txn, PRIVATE_KEY)
        tx_hash = web3.eth.send_raw_transaction(signed_txn.raw_transaction)

        print(f"📤 Transaction sent: {web3.to_hex(tx_hash)}")
        print("⏳ Waiting for confirmation...")

        # Wait for receipt
        tx_receipt = web3.eth.wait_for_transaction_receipt(tx_hash, timeout=300)
        contract_address = tx_receipt.contractAddress

        print(f"✅ Contract deployed at: {contract_address}")
        print(f"🔗 View on Etherscan: https://sepolia.etherscan.io/address/{contract_address}")

        with open('deployed_contract.json', 'w', encoding='utf-8') as output_file:
            json.dump({
                'contract_address': contract_address,
                'network': 'sepolia',
                'abi': abi
            }, output_file, ensure_ascii=False, indent=2)

        # Save to .env
        with open('.env', 'a') as f:
            f.write(f"\nCONTRACT_ADDRESS={contract_address}\n")

        print("💾 Contract address saved to .env file")
        return contract_address

    except Exception as e:
        print(f"❌ Deployment failed: {e}")
        return None

if __name__ == "__main__":
    print("🔧 DrowsinessDetection Contract Deployer")
    print("=" * 50)

    contract_addr = deploy_contract()

    if contract_addr:
        print("\n📋 Next steps:")
        print("1. Copy CONTRACT_ADDRESS to testAmThanh.py")
        print("2. Run: python testAmThanh.py")
        print("3. Test drowsiness detection with MetaMask")
    else:
        print("\n❌ Deployment failed. Check your configuration and try again.")