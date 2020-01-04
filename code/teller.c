#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#include "teller.h"
#include "account.h"
#include "error.h"
#include "debug.h"
#include "branch.h"

/*
 * deposit money into an account
 */
int
Teller_DoDeposit(Bank *bank, AccountNumber accountNum, AccountAmount amount)
{
  assert(amount >= 0);
  if(amount == 0) return ERROR_SUCCESS;

  DPRINTF('t', ("Teller_DoDeposit(account 0x%"PRIx64" amount %"PRId64")\n",
                accountNum, amount));

  Account *account = Account_LookupByNumber(bank, accountNum);

  if (account == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  sem_wait(&account->accLock);
  BranchID BID = (BranchID) (accountNum >> 32);
  sem_wait(&bank->branches[BID].braLock);
  
  Account_Adjust(bank,account, amount, 1);

  sem_post(&account->accLock);
  sem_post(&bank->branches[BID].braLock);
  return ERROR_SUCCESS;
}

/*
 * withdraw money from an account
 */
int
Teller_DoWithdraw(Bank *bank, AccountNumber accountNum, AccountAmount amount)
{
  assert(amount >= 0);
  if(amount == 0) return ERROR_SUCCESS;
  DPRINTF('t', ("Teller_DoWithdraw(account 0x%"PRIx64" amount %"PRId64")\n",
                accountNum, amount));

  Account *account = Account_LookupByNumber(bank, accountNum);

  if (account == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  sem_wait(&account->accLock);

  if (amount > Account_Balance(account)) {
    sem_post(&account->accLock);
    return ERROR_INSUFFICIENT_FUNDS;
  }

  BranchID BID = (BranchID) (accountNum >> 32);
  sem_wait(&bank->branches[BID].braLock);

  Account_Adjust(bank,account, -amount, 1);
  
  sem_post(&account->accLock);
  sem_post(&bank->branches[BID].braLock);
  return ERROR_SUCCESS;
}

/*
 * do a tranfer from one account to another account
 */
int
Teller_DoTransfer(Bank *bank, AccountNumber srcAccountNum,
                  AccountNumber dstAccountNum,
                  AccountAmount amount)
{
  assert(amount >= 0);
  if(dstAccountNum == srcAccountNum) return ERROR_SUCCESS;
  
  DPRINTF('t', ("Teller_DoTransfer(src 0x%"PRIx64", dst 0x%"PRIx64
                ", amount %"PRId64")\n",
                srcAccountNum, dstAccountNum, amount));

  Account *srcAccount = Account_LookupByNumber(bank, srcAccountNum);
  if (srcAccount == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  Account *dstAccount = Account_LookupByNumber(bank, dstAccountNum);
  if (dstAccount == NULL) {
    return ERROR_ACCOUNT_NOT_FOUND;
  }

  if(srcAccount->accountNumber < dstAccount->accountNumber){
    sem_wait(&srcAccount->accLock);
    sem_wait(&dstAccount->accLock);
  } else {
    sem_wait(&dstAccount->accLock);
    sem_wait(&srcAccount->accLock);
  }
    
  if (amount > Account_Balance(srcAccount)) {
    sem_post(&dstAccount->accLock);
    sem_post(&srcAccount->accLock);
    return ERROR_INSUFFICIENT_FUNDS;
  }


  /*
   * If we are doing a transfer within the branch, we tell the Account module to
   * not bother updating the branch balance since the net change for the
   * branch is 0.
   */
  int updateBranch = !Account_IsSameBranch(srcAccountNum, dstAccountNum);


  BranchID srcBID = (BranchID) (srcAccountNum >> 32);
  BranchID dstBID = (BranchID) (dstAccountNum >> 32);

  if(updateBranch){
    if(srcBID < dstBID){
      sem_wait(&bank->branches[srcBID].braLock);
      sem_wait(&bank->branches[dstBID].braLock);
    } else {
      sem_wait(&bank->branches[dstBID].braLock);
      sem_wait(&bank->branches[srcBID].braLock);
    }
  }


  Account_Adjust(bank, srcAccount, -amount, updateBranch);
  Account_Adjust(bank, dstAccount, amount, updateBranch);

  sem_post(&dstAccount->accLock);
  sem_post(&srcAccount->accLock);
  if(updateBranch){
    sem_post(&bank->branches[dstBID].braLock);
    sem_post(&bank->branches[srcBID].braLock);
  }
  return ERROR_SUCCESS;
}
