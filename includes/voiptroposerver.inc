<?php

// $Id$

/**
 * @file
 * Implementation of the VoiptropoServer class
 */


/**
 * File dependencies
 */

module_load_include('inc', 'voip', 'includes/voipserver');
module_load_include('inc', 'voipcall', 'voipcall');


/**
 * Class definitions
 */
        
class VoipTropoServer extends VoipServer {

  /*
   * Constructors
   */

  function __construct() {
    $id = 'tropo';
    $name = 'Tropo';
    $config_form = 'voiptropo_admin_form';
    parent::__construct($id, $name, $config_form);
  }


  /*
   * Public methods
   */

  static function getVoicesList(){
    $voices_available = self::_get_available_voices();
    $list = array();
    foreach($voices_available as $id => $voice) {
      $list[$id] = $voice['description'];
    }
    asort($list);
    return $list;
  }

  /**
   * Implementation of VoipServer() API method
   */
  function getDefaultLanguage(){
    $server_config = $this->getConfig();
    $voice_id = $server_config['voiptropo_voice_id'];
    $voices_available = self::_get_available_voices();
    $voice_descriptor = $voices_available[$voice_id];
    $voice = $voice_descriptor['language'];
    return $voice;
  }

  /**
   * Implementation of VoipServer() API method
   */
  function getDefaultVoice(){
    $server_config = $this->getConfig();
    $voice_id = $server_config['voiptropo_voice_id'];
    $voices_available = self::_get_available_voices();
    $voice_descriptor = $voices_available[$voice_id];
    $voice = $voice_descriptor['voice'];
    return $voice;
  }


  /*
   * API methods
   */

  /**
   * Implementation of the VoipServer dial() API method
   */
  function dial($voipcall, $server_config=NULL) {

    voip_error_reset();

    if(!$server_config) {
      $server_config = $this->getConfig();
    }

    // tropo configuration
	$username = $server_config['voiptropo_account_username'];
	$password = $server_config['voiptropo_account_password'];
    $token = $server_config['voiptropo_voice_token'];

    // Update call variables
    $voipcall->setSource('drupal');
    $voipcall->setDirection(VoipCall::OUTBOUND);
    
    // save the call in order to get generate an nid
    $voipcall->save();

    // Instantiate a new tropo Rest Client
	$client = new SessionAPI();
    //$client = new tropoRestClient($account_sid, $auth_token);

    // Initiate a new outbound call by POST'ing to the Calls resource
    //$tropo_path = "/$api_version/Accounts/$account_sid/Calls";
	
    $caller_number = ($voipcall->getCallerNumber())
                       ? $voipcall->getCallerNumber()
                       : variable_get('voipcall_cid_number', NULL);
    $dest_number = $voipcall->getDestNumber();

    $menu_path = 'voip/tropo/callhandler';
    $call_nid = $voipcall->getNodeId();
    $response_url = $menu_path . '/process_outbound_calls/' . $call_nid;
    $call_handler_url = url($response_url, array('absolute' => TRUE));
    $response_url = $menu_path . '/process_hangup/' . $call_nid;
    $status_callback_url = url($response_url, array('absolute' => TRUE));

    $vars = array(
	  'action' => 'create',
      'username' => $username,
	  'password' => $password,
	  'Caller' => $caller_number,
      'Called' => $dest_number,
      //'Url' => $call_handler_url,
      //'Method' => 'POST',
      //'StatusCallback' => $status_callback_url,
      //'StatusCallbackMethod' => 'POST',
      //'SendDigits' => '',  // TODO: shall we encode extra digits with dest_number?
      //'Timeout' => 60 // TODO: Set to 15 in order to avoid answering machines?
    );
	
	

    //TODO: TZ:What is this?
    //	Handle answering machine options
    /*if($voipcall->getCheckMachine() == TRUE) {
    // TODO: do we need to implement tropo's 'hangup on machine' option here?
      // Note: adding any value in here -- even 'Continue' -- adds a 5 sec delay
      //       to the call. To prevent that, 'IfMachine' has to be NULL
      $vars['IfMachine'] = 'Continue'; 
    }
    */
	try{
	  $response = $client->createSession($token, $vars);
	}
	catch (Exception $e){
	  // differentiate between processing errors and higher-level errors
      //$tropo_error_code = (string)$response->ResponseXml->RestException->Code;
    /*  switch($tropo_error_code){
        // the following codes are listed in
        //   https://www.tropo.com/docs/rest/response_codes.htm
        case '13214': // Dial: Invalid callerId value
        case '21210': // Caller phone number not verified
        case '21212': // Invalid Caller Phone Number
          $error_msg = t('Invalid caller id value');
          $voipcall->setErrorMessage($error_msg);
          $voipcall->setCallStatus(VoipCall::ERROR);
          $voipcall->save();
          break;
        case '13223': // Dial: Invalid phone number format
        case '13224': // Dial: Invalid phone number
        case '21211': // Invalid Called Phone Number
        case '21213': // Caller phone number is required
        case '21401': // Invalid Phone Number
          $error_msg = t('Invalid phone number');
          $voipcall->setErrorMessage($error_msg);
          $voipcall->setCallStatus(VoipCall::ERROR);
          $voipcall->save();
          break;
        case '13225': // Dial: Forbidden phone number 
        case '21215': // Account not authorized to call NNN
          $error_msg = t('Forbidden phone number');
          $voipcall->setErrorMessage($error_msg);
          $voipcall->setCallStatus(VoipCall::ERROR);
          $voipcall->save();
          break;
        default: // processing error
          $tropo_error_msg = $response->ErrorMessage;
          $msg = "Processing failure. Please contact your system's administrator. tropo error @tropo_code: @tropo_msg";
          $params = array('@tropo_code' => $tropo_error_code, 
                          '@tropo_msg' => $tropo_error_msg);
          $voipcall->report_error('voiptropo', $msg, $params);
          break;
      }*/
	    $msg = "Processing failure. Please contact your system's administrator.";
        $voipcall->report_error('voiptropo', $msg);
	    //$voipcall->setErrorMessage($error_msg);
        $voipcall->setCallStatus(VoipCall::ERROR);
        $voipcall->save();
        return FALSE;
	}
    #Else:
    // get the new id and status associated with the call
    //$xml = $response->ResponseXml;
    //$call_id = (string)$xml->Call->Sid;
    //$tropo_call_status = (string)$xml->Call->Status;
    //$voipcall->setCallId($call_id);
    //$voipcall = voiptropo_update_call_status($voipcall, $tropo_call_status);
    $voipcall->save();
    watchdog('voiptropo', 'new call: ' . print_r($voipcall, true));

    return TRUE;
  }


  /**
   * Implementation of the VoipServer hangup() API method
   */
  function hangup($voipcall, $server_config=NULL) {
/*$id = $voipcall->getCallId();
$status = $voipcall->getCallStatus();
watchdog('voiptropo', "in hangup() with call id: $id and status: $status");

    $rc = FALSE;
    voip_error_reset();

    if(!$server_config) {
      $server_config = $this->getConfig();
    }

    // tropo configuration
    $api_version = '2010-04-01';
    $account_sid = $server_config['voiptropo_account_sid'];
    $auth_token = $server_config['voiptropo_auth_token'];

    // Instantiate a new tropo Rest Client
    $client = new tropoRestClient($account_sid, $auth_token);

    $call_id = $voipcall->getCallId();
    if(!$call_id){
      $msg = "Attempt to hangup a tropo call (nid = @nid) without a call id";
      $params = array('@nid' => $voipcall->getNodeId());
      $voipcall->report_error('voiptropo', $msg, $params);
      $rc = FALSE;
    }
    else {
      $tropo_path = "/$api_version/Accounts/$account_sid/Calls/$call_id";
      $status = 'completed';
      $vars =    array(
        'Status' => $status,
      );

      $response = $client->request($tropo_path, "POST", $vars);

      // update call based on response
      if($response->IsError){
        $msg = "tropo hangup command failed: @error";
        $params = array('@error' => $response->ErrorMessage);
        $voipcall->report_error('voiptropo', $msg, $params);
      }
      else {
        $voipcall->setHangupReason(VoipCall::HANGUP_API_REQUEST);
        $voipcall->save();
        $rc = TRUE;
      }
    }

    return $rc;*/
  }


  /**
   * Implementation of the VoipServer ping() API method
   */
  function ping($server_config=NULL) {

    if(!$server_config) {
      $server_config = $this->getConfig();
    }

    // tropo configuration
    $username = $server_config['voiptropo_account_username'];
	$password = $server_config['voiptropo_account_password'];

    // Instantiate a new tropo Rest Client
	$provisioning=new ProvisioningAPI($username, $password);

    // Request a call log as a way to test the server connectivity
    voip_error_reset();
    
	try{
      $response = $provisioning->viewApplications();
	}
	catch (Exception $e){
	  voip_error("Tropo command Ping failed: " . $e->getMessage());
	  return FALSE;
	}
   
    return TRUE;
  }

  /**
   * Private methods
   */
// TODO:  private static function _get_available_voices() {
static function _get_available_voices() {
    $voices = array();
    $voices['carmen'] = array('language' => 'es', 'voice' => 'woman',
                        'description' => t('female voice in Castilian Spanish'));
    $voices['jorge'] = array('language' => 'es', 'voice' => 'man',
                        'description' => t('male voice in Castilian Spanish'));				
	$voices['florence'] = array('language' => 'fr', 'voice' => 'woman',
                        'description' => t('female voice in French'));
    $voices['bernard'] = array('language' => 'fr', 'voice' => 'man',
                        'description' => t('male voice in French'));								
    $voices['allison'] = array('language' => 'us_en', 'voice' => 'woman',
                        'description' => t('female voice in US English'));
    $voices['dave'] = array('language' => 'us_en', 'voice' => 'man',
                        'description' => t('male voice in US English'));
    $voices['kate'] = array('language' => 'en', 'voice' => 'woman',
                        'description' => t('female voice in British English'));
    $voices['simon'] = array('language' => 'en', 'voice' => 'man',
                        'description' => t('male voice in British English'));
	$voices['katrin'] = array('language' => 'de', 'voice' => 'woman',
                        'description' => t('female voice in German'));
    $voices['stefan'] = array('language' => 'de', 'voice' => 'man',
                        'description' => t('male voice in German'));
	$voices['paola'] = array('language' => 'de', 'voice' => 'woman',
                        'description' => t('female voice in Italian'));
    $voices['luca'] = array('language' => 'de', 'voice' => 'man',
                        'description' => t('male voice in Italian'));		
    $voices['saskia'] = array('language' => 'nl', 'voice' => 'woman',
                        'description' => t('female voice in Dutch'));
    $voices['willem'] = array('language' => 'nl', 'voice' => 'man',
                        'description' => t('male voice in Dutch'));
	$voices['soledad'] = array('language' => 'mx', 'voice' => 'woman',
                        'description' => t('female voice in Mexican Spanish'));
    $voices['carlos'] = array('language' => 'mx', 'voice' => 'man',
                        'description' => t('male voice in Mexican Spanish'));
    $voices['zosia'] = array('language' => 'pl', 'voice' => 'woman',
                        'description' => t('female voice in Polish'));
    $voices['krzysztof'] = array('language' => 'pl', 'voice' => 'man',
                        'description' => t('male voice in Polish'));							
    return $voices;
  }

}

