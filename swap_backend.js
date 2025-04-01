const { WalletClient } = require('@arqma/arqma-rpc');

class XEQSwapBackend {
    constructor() {
        // Source wallet client (old XEQ)
        this.sourceWalletClient = new WalletClient({
            host: '127.0.0.1',
            port: 18090,  // Source wallet RPC port
            protocol: 'http'
        });

        // Target wallet client (new XEQ)
        this.targetWalletClient = new WalletClient({
            host: '127.0.0.1',
            port: 18096,  // Target wallet RPC port
            protocol: 'http'
        });
    }

    async getSourceBalance() {
        try {
            const result = await this.sourceWalletClient.getBalance();
            return result;
        } catch (error) {
            console.error('Error getting source balance:', error);
            return null;
        }
    }

    async getTargetBalance() {
        try {
            const result = await this.targetWalletClient.getBalance();
            return result;
        } catch (error) {
            console.error('Error getting target balance:', error);
            return null;
        }
    }

    async transferXEQ(amount, fromSource = true) {
        try {
            if (fromSource) {
                // Transfer from source to target
                const targetAddress = await this.targetWalletClient.getAddress();
                const result = await this.sourceWalletClient.transfer({
                    destinations: [{
                        address: targetAddress,
                        amount: amount
                    }]
                });
                return result;
            } else {
                // Transfer from target to source
                const sourceAddress = await this.sourceWalletClient.getAddress();
                const result = await this.targetWalletClient.transfer({
                    destinations: [{
                        address: sourceAddress,
                        amount: amount
                    }]
                });
                return result;
            }
        } catch (error) {
            console.error('Error transferring XEQ:', error);
            return null;
        }
    }
}

// Example usage
async function main() {
    const swapBackend = new XEQSwapBackend();
    
    try {
        const sourceBalance = await swapBackend.getSourceBalance();
        const targetBalance = await swapBackend.getTargetBalance();
        
        console.log('Source wallet balance:', sourceBalance);
        console.log('Target wallet balance:', targetBalance);
    } catch (error) {
        console.error('Error in main:', error);
    }
}

if (require.main === module) {
    main();
}

module.exports = XEQSwapBackend; 