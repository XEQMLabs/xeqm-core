import sys
import os
from framework.wallet import Wallet

class XEQSwapBackend:
    def __init__(self):
        # Source wallet client (old XEQ)
        self.source_wallet = Wallet(
            protocol='http',
            host='127.0.0.1',
            port=18090  # Source wallet RPC port
        )
        
        # Target wallet client (new XEQ)
        self.target_wallet = Wallet(
            protocol='http',
            host='127.0.0.1',
            port=18096  # Target wallet RPC port
        )

    def get_source_balance(self):
        """Get balance from source wallet"""
        try:
            result = self.source_wallet.get_balance()
            return result
        except Exception as e:
            print(f"Error getting source balance: {e}")
            return None

    def get_target_balance(self):
        """Get balance from target wallet"""
        try:
            result = self.target_wallet.get_balance()
            return result
        except Exception as e:
            print(f"Error getting target balance: {e}")
            return None

    def transfer_xeq(self, amount, from_source=True):
        """Transfer XEQ between wallets"""
        try:
            if from_source:
                # Transfer from source to target
                destinations = [{
                    'address': self.target_wallet.get_address(),
                    'amount': amount
                }]
                result = self.source_wallet.transfer(destinations)
            else:
                # Transfer from target to source
                destinations = [{
                    'address': self.source_wallet.get_address(),
                    'amount': amount
                }]
                result = self.target_wallet.transfer(destinations)
            return result
        except Exception as e:
            print(f"Error transferring XEQ: {e}")
            return None

def main():
    swap_backend = XEQSwapBackend()
    
    # Example usage
    source_balance = swap_backend.get_source_balance()
    target_balance = swap_backend.get_target_balance()
    
    print(f"Source wallet balance: {source_balance}")
    print(f"Target wallet balance: {target_balance}")

if __name__ == "__main__":
    main() 